#define MINIAUDIO_IMPLEMENTATION
#include "transcriber.h"
#include <whisper.h>

static constexpr int    STREAM_INTERVAL_MS  = 2000;  // partial transcription every 2 s
static constexpr int    MIN_SAMPLES         = WHISPER_SAMPLE_RATE / 2;  // need ≥0.5 s

// ============================================================================
// Lifecycle
// ============================================================================

Transcriber::Transcriber() {}

Transcriber::~Transcriber() {
    m_recording = false;
    m_stopCv.notify_all();

    if (m_deviceInit) {
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
    }
    if (m_streamThread.joinable())
        m_streamThread.join();
    if (m_whisperCtx)
        whisper_free(m_whisperCtx);
}

bool Transcriber::Init(const std::string& modelPath) {
    m_whisperCtx = whisper_init_from_file(modelPath.c_str());
    return m_whisperCtx != nullptr;
}

// ============================================================================
// Recording control
// ============================================================================

void Transcriber::StartRecording() {
    if (m_recording || !m_whisperCtx) return;

    // Join any previous streaming thread
    if (m_streamThread.joinable())
        m_streamThread.join();

    {
        std::lock_guard<std::mutex> lk(m_audioMutex);
        m_audioBuffer.clear();
    }

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

    // Signal the streaming thread to wake up and finish
    m_recording = false;
    m_stopCv.notify_all();

    if (m_deviceInit) {
        ma_device_stop(&m_device);
        ma_device_uninit(&m_device);
        m_deviceInit = false;
    }
    // Don't join here — the thread does a final transcription pass
    // asynchronously and calls the callback when done.
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
    // --- Partial results while recording ---
    while (m_recording) {
        {
            std::unique_lock<std::mutex> lk(m_stopMutex);
            m_stopCv.wait_for(lk, std::chrono::milliseconds(STREAM_INTERVAL_MS),
                              [this] { return !m_recording.load(); });
        }
        if (!m_recording) break;

        std::vector<float> audio;
        {
            std::lock_guard<std::mutex> lk(m_audioMutex);
            audio = m_audioBuffer;          // snapshot (copy, not move)
        }
        if (static_cast<int>(audio.size()) < MIN_SAMPLES) continue;

        std::string text = RunWhisper(audio);

        std::lock_guard<std::mutex> lk(m_cbMutex);
        if (m_callback)
            m_callback(text, /*is_final=*/false);
    }

    // --- Final pass after recording stopped ---
    std::vector<float> audio;
    {
        std::lock_guard<std::mutex> lk(m_audioMutex);
        audio.swap(m_audioBuffer);
    }

    std::string text;
    if (!audio.empty())
        text = RunWhisper(audio);

    std::lock_guard<std::mutex> lk(m_cbMutex);
    if (m_callback)
        m_callback(text, /*is_final=*/true);
}

// ============================================================================
// Whisper inference helper
// ============================================================================

std::string Transcriber::RunWhisper(const std::vector<float>& audio) {
    if (!m_whisperCtx || audio.empty()) return "";

    whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress   = false;
    params.print_special    = false;
    params.print_realtime   = false;
    params.print_timestamps = false;
    params.single_segment   = false;
    params.language         = "en";
    params.n_threads        = 4;

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
