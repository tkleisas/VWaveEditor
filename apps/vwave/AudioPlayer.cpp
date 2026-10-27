#include "AudioPlayer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

AudioPlayer::~AudioPlayer() { close(); }

void AudioPlayer::setOutputDevice(std::string device) {
    m_outputDevice = std::move(device);
}

bool AudioPlayer::open() {
    if (m_device != 0) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec requested{};
    requested.freq = 48000;
    requested.format = AUDIO_F32SYS;
    requested.channels = 2;
    requested.samples = 512;
    requested.callback = &AudioPlayer::audioCallback;
    requested.userdata = this;
    SDL_AudioSpec obtained{};
    m_device = SDL_OpenAudioDevice(
        m_outputDevice.empty() ? nullptr : m_outputDevice.c_str(), 0,
        &requested, &obtained, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (m_device == 0) return false;
    m_spec = obtained;
    SDL_PauseAudioDevice(m_device, 1);
    return true;
}

void AudioPlayer::close() {
    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        m_playing.store(false);
        m_tracks.clear();
        SDL_UnlockAudioDevice(m_device);
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
    m_sourcePosition.store(0);
    m_sourceStart = 0;
    m_sourceEnd = 0;
    m_loop = false;
    m_readPosition = 0.0;
}

void AudioPlayer::buildTracks(const vwave::Timeline& timeline) {
    m_tracks.clear();
    const bool solo = timeline.anySolo();
    for (int index = 0; index < timeline.trackCount(); ++index) {
        const auto& track = timeline.track(index);
        if (track.muted || (solo && !track.solo)) continue;
        const vwave::Audio& audio = track.document.audio();
        if (audio.empty() || audio.sampleRate() <= 0) continue;
        PlaybackTrack playback;
        playback.audio = &audio;
        playback.startFrame = track.startFrame;
        playback.sourceStep = static_cast<double>(audio.sampleRate()) /
                              static_cast<double>(m_projectRate);
        const double gain = std::pow(10.0, track.gainDb / 20.0);
        double left = 1.0;
        double right = 1.0;
        if (audio.numChannels() == 1) {
            const double angle = (track.pan + 1.0) * kPi / 4.0;
            left = std::cos(angle);
            right = std::sin(angle);
        } else if (track.pan < 0.0) {
            right = 1.0 + track.pan;
        } else if (track.pan > 0.0) {
            left = 1.0 - track.pan;
        }
        playback.leftGain = static_cast<float>(left * gain);
        playback.rightGain = static_cast<float>(right * gain);
        m_tracks.push_back(playback);
    }
}

bool AudioPlayer::play(const vwave::Timeline& timeline, int64_t startFrame,
                       int64_t endFrame, bool loop) {
    if (m_device == 0 || timeline.empty()) return false;
    const vwave::Range range = vwave::Range{startFrame, endFrame}.clamped(
        timeline.lengthFrames());
    if (range.empty()) return false;

    SDL_LockAudioDevice(m_device);
    m_projectRate = timeline.sampleRate();
    m_timelineStep = static_cast<double>(m_projectRate) /
                     static_cast<double>(m_spec.freq);
    m_sourceStart = range.start;
    m_sourceEnd = range.end;
    m_loop = loop;
    m_readPosition = static_cast<double>(range.start);
    m_sourcePosition.store(range.start);
    buildTracks(timeline);
    m_playing.store(true);
    SDL_UnlockAudioDevice(m_device);
    SDL_PauseAudioDevice(m_device, 0);
    return true;
}

void AudioPlayer::updateMix(const vwave::Timeline& timeline) {
    if (m_device == 0) return;
    SDL_LockAudioDevice(m_device);
    buildTracks(timeline);
    SDL_UnlockAudioDevice(m_device);
}

void AudioPlayer::stop() {
    if (m_device == 0) return;
    SDL_PauseAudioDevice(m_device, 1);
    SDL_LockAudioDevice(m_device);
    m_playing.store(false);
    m_tracks.clear();
    m_readPosition = static_cast<double>(m_sourceStart);
    SDL_UnlockAudioDevice(m_device);
    m_sourcePosition.store(m_sourceStart);
}

void AudioPlayer::audioCallback(void* userdata, Uint8* stream, int length) {
    static_cast<AudioPlayer*>(userdata)->render(stream, length);
}

void AudioPlayer::render(Uint8* stream, int length) {
    float* output = reinterpret_cast<float*>(stream);
    const int64_t requested =
        static_cast<int64_t>(length / static_cast<int>(sizeof(float) * 2));
    std::fill_n(output, requested * 2, 0.0f);
    if (!m_playing.load(std::memory_order_acquire)) return;

    const double endPosition = static_cast<double>(m_sourceEnd);
    const double startPosition = static_cast<double>(m_sourceStart);
    for (int64_t frame = 0; frame < requested; ++frame) {
        const double timelinePosition = m_readPosition;
        for (const PlaybackTrack& track : m_tracks) {
            const vwave::Audio* audio = track.audio;
            if (!audio) continue;
            const int channels = audio->numChannels();
            if (channels <= 0) continue;
            const int64_t frames = audio->numFrames();
            const float* left = audio->channel(0);
            const float* right = channels > 1 ? audio->channel(1) : left;
            const double position =
                (timelinePosition - static_cast<double>(track.startFrame)) *
                track.sourceStep;
            if (position >= 0.0 && position < static_cast<double>(frames)) {
                const int64_t index =
                    static_cast<int64_t>(std::floor(position));
                const int64_t next = std::min(index + 1, frames - 1);
                const float fraction =
                    static_cast<float>(position - static_cast<double>(index));
                const float a = left[index];
                const float b = left[next];
                const float c = right[index];
                const float d = right[next];
                output[frame * 2] +=
                    (a + (b - a) * fraction) * track.leftGain;
                output[frame * 2 + 1] +=
                    (c + (d - c) * fraction) * track.rightGain;
            }
        }
        m_readPosition += m_timelineStep;
        if (m_readPosition >= endPosition) {
            if (m_loop) {
                const double length = endPosition - startPosition;
                const double overflow = m_readPosition - endPosition;
                m_readPosition =
                    length > 0.0
                        ? startPosition + std::fmod(overflow, length)
                        : endPosition;
            } else {
                m_readPosition = endPosition;
                m_playing.store(false, std::memory_order_release);
                break;
            }
        }
    }

    const int64_t position = std::clamp<int64_t>(
        static_cast<int64_t>(std::floor(m_readPosition)),
        m_sourceStart, m_sourceEnd);
    m_sourcePosition.store(position);
}
