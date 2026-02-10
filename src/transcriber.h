#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <condition_variable>

#include <miniaudio.h>

struct whisper_context;

class Transcriber {
public:
    Transcriber();
    ~Transcriber();

    bool Init(const std::string& modelPath);

    void StartRecording();
    void StopRecording();              // stop mic + skip final pass (non-blocking)
    void CancelRecording();            // same as Stop but semantically "discard"
    bool IsRecording() const { return m_recording.load(); }

    /// Callback receives (transcribed_text, is_final).
    /// Called from a background thread.
    void SetCallback(std::function<void(const std::string&, bool)> cb) {
        std::lock_guard<std::mutex> lk(m_cbMutex);
        m_callback = std::move(cb);
    }

private:
    static void AudioDataCallback(ma_device* pDevice, void* pOutput,
                                  const void* pInput, ma_uint32 frameCount);

    /// Background thread: periodically transcribes while recording.
    void StreamingLoop();

    /// Run whisper inference on audio samples.
    /// @param partial  If true, uses single-segment mode for speed.
    std::string RunWhisper(const std::vector<float>& audio, bool partial);

    /// Stop the audio device (idempotent).
    void StopDevice();

    whisper_context* m_whisperCtx = nullptr;

    ma_device m_device     = {};
    bool      m_deviceInit = false;

    std::vector<float> m_audioBuffer;
    std::mutex         m_audioMutex;
    std::atomic<bool>  m_recording{false};
    std::atomic<bool>  m_cancelled{false};        // true → skip final pass entirely
    std::atomic<bool>  m_abortInference{false};   // true → whisper_full returns early
    std::atomic<bool>  m_threadDone{true};         // true when streaming thread has exited

    std::thread             m_streamThread;
    std::mutex              m_stopMutex;
    std::condition_variable m_stopCv;

    std::string m_confirmedText;           // accumulated text from committed chunks

    std::function<void(const std::string&, bool)> m_callback;
    std::mutex                                    m_cbMutex;

    std::thread             m_warmupThread;          // runs a throwaway inference at startup
    std::atomic<bool>       m_warmupDone{false};     // true once warmup inference finishes
};
