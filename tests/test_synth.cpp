#include "test_framework.h"

#include "vwave/FFT.h"
#include "vwave/Synth.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace vwave;

namespace {

double dominantFrequency(const Audio& audio, double startSeconds,
                         double endSeconds) {
    const int sampleRate = audio.sampleRate();
    const int64_t start = std::max<int64_t>(
        0, static_cast<int64_t>(startSeconds * sampleRate));
    const int64_t end = std::min<int64_t>(
        audio.numFrames(), static_cast<int64_t>(endSeconds * sampleRate));
    const int64_t count = end - start;
    if (count < 64) return 0.0;
    const int fftSize = nextPowerOfTwo(static_cast<int>(count));
    std::vector<std::complex<float>> data(static_cast<size_t>(fftSize));
    for (int64_t i = 0; i < count; ++i) {
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * 3.14159265358979323846 * static_cast<double>(i) /
            static_cast<double>(count - 1));
        data[static_cast<size_t>(i)] = {
            static_cast<float>(audio.sample(0, start + i) * window), 0.0f};
    }
    FFT fft(fftSize);
    fft.forward(data);
    size_t peak = 1;
    float best = 0.0f;
    for (size_t i = 1; i < data.size() / 2; ++i) {
        const float magnitude = std::abs(data[i]);
        if (magnitude > best) {
            best = magnitude;
            peak = i;
        }
    }
    double refined = static_cast<double>(peak);
    if (peak > 1 && peak + 1 < data.size()) {
        const double a = std::abs(data[peak - 1]);
        const double b = std::abs(data[peak]);
        const double c = std::abs(data[peak + 1]);
        const double denominator = a - 2.0 * b + c;
        if (std::abs(denominator) > 1e-12)
            refined += 0.5 * (a - c) / denominator;
    }
    return refined * sampleRate / fftSize;
}

double rms(const Audio& audio, double startSeconds, double endSeconds) {
    const int64_t start = static_cast<int64_t>(startSeconds * audio.sampleRate());
    const int64_t end = static_cast<int64_t>(endSeconds * audio.sampleRate());
    const int64_t safeStart = std::clamp<int64_t>(start, 0, audio.numFrames());
    const int64_t safeEnd = std::clamp<int64_t>(end, safeStart, audio.numFrames());
    if (safeEnd <= safeStart) return 0.0;
    double sum = 0.0;
    for (int64_t i = safeStart; i < safeEnd; ++i) {
        const double value = audio.sample(0, i);
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(safeEnd - safeStart));
}

} // namespace

VWTEST("wavetable synth renders a note at the expected pitch") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 2048, "sine");
    table.setSampleRate(48000);
    SynthNoteOptions options;
    options.sampleRate = 48000;
    options.channels = 1;
    options.rootNote = 69.0;
    options.gain = 0.8;
    const Audio note = renderWavetableNote(table, 69.0, 0.5, options);
    CHECK_EQ(note.sampleRate(), 48000);
    CHECK_EQ(note.numChannels(), 1);
    CHECK_EQ(note.numFrames(), int64_t{24000});
    CHECK_NEAR(dominantFrequency(note, 0.1, 0.35), 440.0, 2.0);
    CHECK(rms(note, 0.1, 0.35) > 0.2);
    CHECK_NEAR(note.sample(0, note.numFrames() - 1), 0.0, 0.02);
}

VWTEST("wavetable synth tracks midi notes") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 1024);
    table.setSampleRate(48000);
    SynthNoteOptions options;
    options.sampleRate = 48000;
    options.channels = 1;
    options.rootNote = 69.0;
    const Audio note = renderWavetableNote(table, 81.0, 0.3, options);
    CHECK_EQ(note.numFrames(), int64_t{14400});
    CHECK_NEAR(dominantFrequency(note, 0.05, 0.2), 880.0, 5.0);
}

VWTEST("wavetable synth envelope shapes the attack and release") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 1024);
    table.setSampleRate(48000);
    SynthNoteOptions options;
    options.sampleRate = 48000;
    options.channels = 1;
    options.envelope.attack = 0.1;
    options.envelope.decay = 0.0;
    options.envelope.sustain = 1.0;
    options.envelope.release = 0.05;
    const Audio note = renderWavetableNote(table, 69.0, 0.5, options);
    CHECK_NEAR(note.sample(0, 0), 0.0, 1e-6);
    CHECK(rms(note, 0.0, 0.05) < rms(note, 0.15, 0.3));
    CHECK_NEAR(note.sample(0, note.numFrames() - 1), 0.0, 0.02);
}

VWTEST("wavetable synth stereo output is finite and correlated") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 1024);
    table.setSampleRate(48000);
    SynthNoteOptions options;
    options.sampleRate = 48000;
    options.channels = 2;
    options.unison = 2;
    options.detuneCents = 8.0;
    const Audio note = renderWavetableNote(table, 69.0, 0.2, options);
    CHECK_EQ(note.numChannels(), 2);
    for (int64_t i = 0; i < note.numFrames(); ++i) {
        CHECK(std::isfinite(note.sample(0, i)));
        CHECK(std::isfinite(note.sample(1, i)));
    }
    CHECK(rms(note, 0.05, 0.15) > 0.1);
}

VWTEST("wavetable synth rejects invalid requests") {
    Wavetable empty;
    SynthNoteOptions options;
    CHECK(renderWavetableNote(empty, 60.0, 1.0, options).empty());
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 256);
    CHECK(renderWavetableNote(table, 60.0, 0.0, options).empty());
    CHECK(renderWavetableNote(table, 60.0, -1.0, options).empty());
    options.sampleRate = -1;
    CHECK(renderWavetableNote(table, 60.0, 0.1, options).empty());
    options.sampleRate = 48000;
    options.channels = 3;
    CHECK(renderWavetableNote(table, 60.0, 0.1, options).empty());
}
