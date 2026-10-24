#include "AudioPlayer.h"

#include "vwave/Processing.h"

#include <algorithm>
#include <cstring>

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open() {
    if (m_device != 0) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec requested{};
    requested.freq = 48000;
    requested.format = AUDIO_F32SYS;
    requested.channels = 2;
    requested.samples = 1024;
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
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
    m_buffer.clear();
    m_playing.store(false);
    m_readFrame.store(0);
    m_sourcePosition.store(0);
}

bool AudioPlayer::play(const vwave::Audio& audio, int64_t startFrame, int64_t endFrame) {
    if (m_device == 0) return false;
    const vwave::Range range = vwave::Range{startFrame, endFrame}.clamped(audio.numFrames());
    if (range.empty()) return false;
    vwave::Audio selected = audio.slice(range);
    if (selected.sampleRate() != m_spec.freq)
        selected = vwave::resample(selected, m_spec.freq);
    if (selected.empty()) return false;

    std::vector<float> buffer(static_cast<size_t>(selected.numFrames()) * 2,
                              0.0f);
    for (int64_t frame = 0; frame < selected.numFrames(); ++frame) {
        float left = selected.sample(0, frame);
        float right = selected.numChannels() > 1 ? selected.sample(1, frame)
                                                 : left;
        buffer[static_cast<size_t>(frame) * 2] = left;
        buffer[static_cast<size_t>(frame) * 2 + 1] = right;
    }

    SDL_LockAudioDevice(m_device);
    m_buffer = std::move(buffer);
    m_readFrame.store(0);
    m_sourceStart = range.start;
    m_sourceRatio = static_cast<double>(audio.sampleRate()) /
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
    m_playing.store(false);
    m_readFrame.store(0);
    m_sourcePosition.store(m_sourceStart);
}

void AudioPlayer::audioCallback(void* userdata, Uint8* stream, int length) {
    static_cast<AudioPlayer*>(userdata)->render(stream, length);
}

void AudioPlayer::render(Uint8* stream, int length) {
    const int64_t requested =
        static_cast<int64_t>(length / static_cast<int>(sizeof(float) * 2));
    int64_t position = m_readFrame.load(std::memory_order_relaxed);
    const int64_t available =
        static_cast<int64_t>(m_buffer.size() / 2);
    float* output = reinterpret_cast<float*>(stream);
    for (int64_t frame = 0; frame < requested; ++frame) {
        const int64_t source = position + frame;
        if (source < available) {
            output[frame * 2] = m_buffer[static_cast<size_t>(source) * 2];
            output[frame * 2 + 1] =
                m_buffer[static_cast<size_t>(source) * 2 + 1];
        } else {
            output[frame * 2] = 0.0f;
            output[frame * 2 + 1] = 0.0f;
        }
    }
    const int64_t next = std::min(position + requested, available);
    m_readFrame.store(next, std::memory_order_relaxed);
    m_sourcePosition.store(
        m_sourceStart + static_cast<int64_t>(
                            static_cast<double>(next) * m_sourceRatio),
        std::memory_order_relaxed);
    if (next >= available) m_playing.store(false, std::memory_order_relaxed);
}
