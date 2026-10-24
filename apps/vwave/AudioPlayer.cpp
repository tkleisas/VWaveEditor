#include "AudioPlayer.h"

#include <algorithm>
#include <cmath>

AudioPlayer::~AudioPlayer() { close(); }

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
    m_device = SDL_OpenAudioDevice(nullptr, 0, &requested, &obtained,
                                   SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (m_device == 0) return false;
    m_spec = obtained;
    SDL_PauseAudioDevice(m_device, 1);
    return true;
}

void AudioPlayer::close() {
    if (m_device != 0) {
        SDL_LockAudioDevice(m_device);
        m_playing.store(false);
        m_source.store(nullptr);
        SDL_UnlockAudioDevice(m_device);
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
    m_sourcePosition.store(0);
    m_sourceStart = 0;
    m_sourceEnd = 0;
    m_readPosition = 0.0;
}

bool AudioPlayer::play(const vwave::Audio& audio, int64_t startFrame,
                       int64_t endFrame) {
    if (m_device == 0 || audio.empty() || audio.sampleRate() <= 0) return false;
    const vwave::Range range =
        vwave::Range{startFrame, endFrame}.clamped(audio.numFrames());
    if (range.empty()) return false;

    SDL_LockAudioDevice(m_device);
    m_source.store(&audio);
    m_sourceStart = range.start;
    m_sourceEnd = range.end;
    m_readPosition = static_cast<double>(range.start);
    m_sourceStep = static_cast<double>(audio.sampleRate()) /
                   static_cast<double>(m_spec.freq);
    m_sourcePosition.store(range.start);
    m_playing.store(true);
    SDL_UnlockAudioDevice(m_device);
    SDL_PauseAudioDevice(m_device, 0);
    return true;
}

void AudioPlayer::stop() {
    if (m_device == 0) return;
    SDL_PauseAudioDevice(m_device, 1);
    SDL_LockAudioDevice(m_device);
    m_playing.store(false);
    m_source.store(nullptr);
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
    const vwave::Audio* source = m_source.load(std::memory_order_acquire);
    if (!m_playing.load(std::memory_order_acquire) || !source) {
        std::fill_n(output, requested * 2, 0.0f);
        return;
    }

    const int channels = source->numChannels();
    const int64_t frames = source->numFrames();
    const float* left = source->channel(0);
    const float* right = channels > 1 ? source->channel(1) : left;
    double position = m_readPosition;
    int64_t outputFrame = 0;
    while (outputFrame < requested && position < m_sourceEnd) {
        const int64_t index = static_cast<int64_t>(std::floor(position));
        const int64_t next = std::min(index + 1, frames - 1);
        const float fraction =
            static_cast<float>(position - static_cast<double>(index));
        const float a = index >= 0 && index < frames ? left[index] : 0.0f;
        const float b = next >= 0 && next < frames ? left[next] : 0.0f;
        const float c = index >= 0 && index < frames ? right[index] : 0.0f;
        const float d = next >= 0 && next < frames ? right[next] : 0.0f;
        output[outputFrame * 2] = a + (b - a) * fraction;
        output[outputFrame * 2 + 1] = c + (d - c) * fraction;
        position += m_sourceStep;
        ++outputFrame;
    }
    if (outputFrame < requested)
        std::fill_n(output + outputFrame * 2,
                    (requested - outputFrame) * 2, 0.0f);

    m_readPosition = position;
    const int64_t sourcePosition =
        std::min<int64_t>(static_cast<int64_t>(position), m_sourceEnd);
    m_sourcePosition.store(sourcePosition);
    if (position >= m_sourceEnd) m_playing.store(false);
}
