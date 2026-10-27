#pragma once

#include "vwave/Audio.h"
#include "vwave/Timeline.h"

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool open();
    void close();
    void setOutputDevice(std::string device);

    bool play(const vwave::Timeline& timeline, int64_t startFrame,
              int64_t endFrame, bool loop = false);
    void updateMix(const vwave::Timeline& timeline);
    void stop();

    bool isPlaying() const { return m_playing.load(); }
    int64_t sourcePosition() const { return m_sourcePosition.load(); }
    int64_t sourceStart() const { return m_sourceStart; }
    int sampleRate() const { return m_spec.freq; }

private:
    struct PlaybackTrack {
        const vwave::Audio* audio = nullptr;
        int64_t startFrame = 0;
        double sourceStep = 1.0;
        float leftGain = 1.0f;
        float rightGain = 1.0f;
    };

    static void audioCallback(void* userdata, Uint8* stream, int length);
    void render(Uint8* stream, int length);
    void buildTracks(const vwave::Timeline& timeline);

    SDL_AudioDeviceID m_device = 0;
    SDL_AudioSpec m_spec{};
    std::string m_outputDevice;
    std::vector<PlaybackTrack> m_tracks;
    std::atomic<bool> m_playing{false};
    std::atomic<int64_t> m_sourcePosition{0};
    int64_t m_sourceStart = 0;
    int64_t m_sourceEnd = 0;
    bool m_loop = false;
    int m_projectRate = 48000;
    double m_readPosition = 0.0;
    double m_timelineStep = 1.0;
};
