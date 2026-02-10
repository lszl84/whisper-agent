#define MINIAUDIO_IMPLEMENTATION
#include "transcriber.h"
#include <whisper.h>
#include <chrono>
#include <algorithm>

static constexpr int INITIAL_INTERVAL_MS   = 300;                      // first partial fires quickly
static constexpr int STREAM_INTERVAL_MS    = 400;                      // subsequent partials
static constexpr int MIN_SAMPLES           = WHISPER_SAMPLE_RATE / 4;  // need ≥0.25 s of audio
static constexpr int COMMIT_SAMPLES        = WHISPER_SAMPLE_RATE * 25; // commit chunk every 25 s
static constexpr int SHUTDOWN_TIMEOUT_MS   = 200;                      // max wait for thread on shutdown

static int inferenceThreadCount() {
    unsigned n = std::thread::hardware_concurrency();
    return static_cast<int>(std::max(4u, std::min(n, 16u)));
}

// ============================================================================
// Lifecycle
// ============================================================================

Transcriber::Transcriber() {}

Transcriber::~Transcriber() {
    m_recording       = false;
    m_cancelled       = true;
    m_abortInference  = true;
    m_stopCv.notify_all();

    if (m_warmupThread.joinable())
        m_warmupThread.join();

    StopDevice();

    if (m_streamThread.joinable()) {
        // Give the thread a moment to notice the abort flag and exit.
        // If it's stuck inside whisper's encoder pass (which can't be
        // interrupted), detach it — the process is exiting anyway and
        // the OS will reclaim resources.
        {
            std::unique_lock<std::mutex> lk(m_stopMutex);
            m_stopCv.wait_for(lk, std::chrono::milliseconds(SHUTDOWN_TIMEOUT_MS),
                              [this] { return m_threadDone.load(); });
        }
        if (m_threadDone.load()) {
            m_streamThread.join();
        } else {
            m_streamThread.detach();
            m_whisperCtx = nullptr;   // thread still owns it — don't free
        }
    }

    if (m_whisperCtx)
        whisper_free(m_whisperCtx);
}

bool Transcriber::Init(const std::string& modelPath) {
    m_whisperCtx = whisper_init_from_file(modelPath.c_str());
    if (!m_whisperCtx) return false;

    // Run a throwaway inference on silence so whisper pre-allocates its
    // internal buffers now instead of on the first real recording.
    // This runs on a background thread so the UI isn't blocked.
    // The streaming loop polls m_warmupDone before its first inference —
    // audio capture starts immediately regardless.
    m_warmupThread = std::thread([this] {
        std::vector<float> silence(WHISPER_SAMPLE_RATE / 2, 0.0f); // 0.5 s
        RunWhisper(silence, /*partial=*/true);
        m_warmupDone = true;
    });

    return true;
}

// ============================================================================
// Recording control
// ============================================================================

void Transcriber::StartRecording() {
    if (m_recording || !m_whisperCtx) return;

    // Ensure any previous streaming thread is fully stopped.
    m_cancelled      = true;
    m_abortInference = true;
    m_stopCv.notify_all();
    if (m_streamThread.joinable())
        m_streamThread.join();

    // Reset flags for the new session
    m_cancelled      = false;
    m_abortInference = false;
    m_threadDone     = false;

    {
        std::lock_guard<std::mutex> lk(m_audioMutex);
        m_audioBuffer.clear();
    }
    m_confirmedText.clear();

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format   = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate       = WHISPER_SAMPLE_RATE;
    cfg.dataCallback     = AudioDataCallback;
    cfg.pUserData        = this;

    if (m_deviceInit) {
        ma_device_uninit(&m_device);
        m_deviceInit = false;
    }
    if (ma_device_init(nullptr, &cfg, &m_device) != MA_SUCCESS)
        return;
    m_deviceInit = true;

    if (ma_device_start(&m_device) != MA_SUCCESS) {
        ma_device_uninit(&m_device);
        m_deviceInit = false;
        return;
    }

    m_recording = true;
    m_streamThread = std::thread(&Transcriber::StreamingLoop, this);
}

void Transcriber::StopRecording() {
    if (!m_recording) return;

    // Stop the microphone and tell the streaming loop to exit without
    // doing a final pass.  The UI will keep whatever text the last
    // partial produced — no need to wait for another inference.
    m_recording      = false;
    m_cancelled      = true;
    m_abortInference = true;
    m_stopCv.notify_all();

    StopDevice();
    // Thread exits on its own.  Joined in StartRecording() or destructor.
}

void Transcriber::CancelRecording() {
    // Identical flags to StopRecording — just a semantic alias for
    // "discard the result".
    if (!m_recording && !m_streamThread.joinable()) return;

    m_recording      = false;
    m_cancelled      = true;
    m_abortInference = true;
    m_stopCv.notify_all();

    StopDevice();
    // Thread exits on its own.  Joined in StartRecording() or destructor.
}

void Transcriber::StopDevice() {
    if (m_deviceInit) {
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
        m_deviceInit = false;
    }
}

// ============================================================================
// miniaudio capture callback (audio thread)
// ============================================================================

void Transcriber::AudioDataCallback(ma_device* pDevice, void* /*pOutput*/,
                                    const void* pInput, ma_uint32 frameCount)
{
    auto* self = static_cast<Transcriber*>(pDevice->pUserData);
    if (!pInput || !self->m_recording) return;

    const auto* samples = static_cast<const float*>(pInput);
    std::lock_guard<std::mutex> lk(self->m_audioMutex);
    self->m_audioBuffer.insert(self->m_audioBuffer.end(),
                               samples, samples + frameCount);
}

// ============================================================================
// Streaming loop (background thread)
// ============================================================================

void Transcriber::StreamingLoop() {
    // Wait for the startup warmup inference to finish before touching
    // the whisper context.  Audio is already being captured into
    // m_audioBuffer while we wait, so nothing the user says is lost.
    while (!m_warmupDone.load()) {
        if (m_cancelled.load()) {
            m_threadDone = true;
            m_stopCv.notify_all();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    bool firstIter = true;
    std::string lastPartialText;

    while (m_recording && !m_cancelled) {
        int interval = firstIter ? INITIAL_INTERVAL_MS : STREAM_INTERVAL_MS;
        firstIter = false;

        {
            std::unique_lock<std::mutex> lk(m_stopMutex);
            m_stopCv.wait_for(lk, std::chrono::milliseconds(interval),
                              [this] { return !m_recording.load() || m_cancelled.load(); });
        }
        if (!m_recording || m_cancelled) break;

        // Snapshot the audio buffer.  When it exceeds the commit
        // threshold, save the current partial text as confirmed and
        // clear the buffer so inference stays fast.
        std::vector<float> audio;
        {
            std::lock_guard<std::mutex> lk(m_audioMutex);

            if (m_audioBuffer.size() > static_cast<size_t>(COMMIT_SAMPLES)
                && !lastPartialText.empty())
            {
                if (m_confirmedText.empty())
                    m_confirmedText = lastPartialText;
                else
                    m_confirmedText += " " + lastPartialText;
                m_audioBuffer.clear();
                lastPartialText.clear();
            }

            audio = m_audioBuffer;
        }
        if (static_cast<int>(audio.size()) < MIN_SAMPLES) continue;

        m_abortInference = false;  // allow this inference to run
        std::string text = RunWhisper(audio, /*partial=*/true);
        if (m_abortInference || m_cancelled) break;  // aborted mid-inference

        lastPartialText = text;

        // Build full display: confirmed chunks + current partial
        std::string displayText;
        if (m_confirmedText.empty())
            displayText = text;
        else if (text.empty())
            displayText = m_confirmedText;
        else
            displayText = m_confirmedText + " " + text;

        std::lock_guard<std::mutex> lk(m_cbMutex);
        if (m_callback)
            m_callback(displayText, /*is_final=*/false);
    }

    // Signal that the thread is done so the destructor doesn't block.
    m_threadDone = true;
    m_stopCv.notify_all();
}

// ============================================================================
// Whisper inference helper
// ============================================================================

std::string Transcriber::RunWhisper(const std::vector<float>& audio, bool partial) {
    if (!m_whisperCtx || audio.empty()) return "";

    whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress   = false;
    params.print_special    = false;
    params.print_realtime   = false;
    params.print_timestamps = false;
    params.single_segment   = partial;   // faster for partial previews
    params.language         = "en";
    params.n_threads        = inferenceThreadCount();

    // Allow aborting inference when the user cancels or stops
    params.abort_callback = [](void* data) -> bool {
        return static_cast<Transcriber*>(data)->m_abortInference.load();
    };
    params.abort_callback_user_data = this;

    if (whisper_full(m_whisperCtx, params,
                     audio.data(), static_cast<int>(audio.size())) != 0)
        return "";

    std::string result;
    int nSeg = whisper_full_n_segments(m_whisperCtx);
    for (int i = 0; i < nSeg; ++i) {
        const char* seg = whisper_full_get_segment_text(m_whisperCtx, i);
        if (seg) result += seg;
    }

    // Trim whitespace
    auto s = result.find_first_not_of(" \t\n\r");
    auto e = result.find_last_not_of(" \t\n\r");
    if (s != std::string::npos && e != std::string::npos)
        result = result.substr(s, e - s + 1);
    else
        result.clear();

    return result;
}
