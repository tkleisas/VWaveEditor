#pragma once

#include "vwave/Audio.h"

#include <SDL.h>

#include <atomic>
#include <cstdint>

class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool open();
    void close();

    bool play(const vwave::Audio& audio, int64_t startFrame, int64_t endFrame);
    void stop();

    bool isPlaying() const { return m_playing.load(); }
    int64_t sourcePosition() const { return m_sourcePosition.load(); }
    int64_t sourceStart() const { return m_sourceStart; }
    int sampleRate() const { return m_spec.freq; }

private:
    static void audioCallback(void* userdata, Uint8* stream, int length);
    void render(Uint8* stream, int length);

    SDL_AudioDeviceID m_device = 0;
    SDL_AudioSpec m_spec{};
    std::atomic<const vwave::Audio*> m_source{nullptr};
    std::atomic<bool> m_playing{false};
    std::atomic<int64_t> m_sourcePosition{0};
    int64_t m_sourceStart = 0;
    int64_t m_sourceEnd = 0;
    double m_readPosition = 0.0;
    double m_sourceStep = 1.0;
};
