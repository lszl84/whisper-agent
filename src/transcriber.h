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
    void StopRecording();
    bool IsRecording() const { return m_recording.load(); }

    /// Callback receives (transcribed_text, is_final).
    /// Partial results arrive while recording; the final result arrives after Stop.
    /// Called from a background thread.
    void SetCallback(std::function<void(const std::string&, bool)> cb) {
        std::lock_guard<std::mutex> lk(m_cbMutex);
        m_callback = std::move(cb);
    }

private:
    static void AudioDataCallback(ma_device* pDevice, void* pOutput,
                                  const void* pInput, ma_uint32 frameCount);

    /// Background thread: periodically transcribes, then does a final pass.
    void StreamingLoop();

    /// Run whisper inference on audio samples. Thread-safe via m_whisperMutex.
    std::string RunWhisper(const std::vector<float>& audio);

    whisper_context* m_whisperCtx = nullptr;

    ma_device m_device     = {};
    bool      m_deviceInit = false;

    std::vector<float> m_audioBuffer;
    std::mutex         m_audioMutex;
    std::atomic<bool>  m_recording{false};

    std::thread             m_streamThread;
    std::mutex              m_stopMutex;
    std::condition_variable m_stopCv;

    std::function<void(const std::string&, bool)> m_callback;
    std::mutex                                    m_cbMutex;
};
