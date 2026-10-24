#pragma once

#include "vwave/RingBuffer.h"

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <vector>

class AudioRecorder {
public:
    AudioRecorder() = default;
    ~AudioRecorder();

    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    bool start(const char* deviceName, int sampleRate = 48000);
    void stop();

    bool isRecording() const { return m_recording.load(); }
    int sampleRate() const { return m_spec.freq; }
    int channels() const { return m_spec.channels; }
    size_t drain(std::vector<float>& destination);
    size_t droppedSamples() const { return m_dropped.load(); }

private:
    static void captureCallback(void* userdata, Uint8* stream, int length);
    void capture(const Uint8* stream, int length);

    SDL_AudioDeviceID m_device = 0;
    SDL_AudioSpec m_spec{};
    vwave::FloatRingBuffer m_ring{1u << 20};
    std::atomic<bool> m_recording{false};
    std::atomic<size_t> m_dropped{0};
};
