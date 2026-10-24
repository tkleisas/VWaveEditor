#include "AudioRecorder.h"

#include <algorithm>

AudioRecorder::~AudioRecorder() { stop(); }

bool AudioRecorder::start(const char* deviceName, int sampleRate) {
    stop();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec requested{};
    requested.freq = sampleRate > 0 ? sampleRate : 48000;
    requested.format = AUDIO_F32SYS;
    requested.channels = 2;
    requested.samples = 512;
    requested.callback = &AudioRecorder::captureCallback;
    requested.userdata = this;
    SDL_AudioSpec obtained{};
    m_device = SDL_OpenAudioDevice(
        deviceName, 1, &requested, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (m_device == 0) {
        requested.channels = 1;
        m_device = SDL_OpenAudioDevice(
            deviceName, 1, &requested, &obtained,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    }
    if (m_device == 0) return false;
    m_spec = obtained;
    m_ring.clear();
    m_dropped.store(0);
    m_recording.store(true);
    SDL_PauseAudioDevice(m_device, 0);
    return true;
}

void AudioRecorder::stop() {
    if (m_device == 0) return;
    SDL_PauseAudioDevice(m_device, 1);
    m_recording.store(false);
    SDL_CloseAudioDevice(m_device);
    m_device = 0;
}

size_t AudioRecorder::drain(std::vector<float>& destination) {
    const size_t available = m_ring.availableRead();
    if (available == 0) return 0;
    const size_t offset = destination.size();
    destination.resize(offset + available);
    const size_t copied = m_ring.pop(destination.data() + offset, available);
    destination.resize(offset + copied);
    return copied;
}

void AudioRecorder::captureCallback(void* userdata, Uint8* stream, int length) {
    static_cast<AudioRecorder*>(userdata)->capture(stream, length);
}

void AudioRecorder::capture(const Uint8* stream, int length) {
    const size_t samples = static_cast<size_t>(length) / sizeof(float);
    const float* input = reinterpret_cast<const float*>(stream);
    if (!m_ring.push(input, samples))
        m_dropped.fetch_add(samples);
}
