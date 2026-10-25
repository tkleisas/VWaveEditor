#include "test_framework.h"

#include "vwave/Document.h"
#include "vwave/FFT.h"
#include "vwave/Spectral.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

Audio makeTone(double frequency, double amplitude, double seconds,
               int sampleRate = 48000) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(1, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(
            amplitude * std::sin(2.0 * kPi * frequency *
                                 static_cast<double>(i) / sampleRate));
    }
    return audio;
}

Audio makeMix(const std::vector<double>& frequencies,
              const std::vector<double>& amplitudes, double seconds) {
    const int sampleRate = 48000;
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(1, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        double value = 0.0;
        for (size_t tone = 0; tone < frequencies.size(); ++tone) {
            value += amplitudes[tone] * std::sin(
                2.0 * kPi * frequencies[tone] * static_cast<double>(i) /
                sampleRate);
        }
        audio.sample(0, i) = static_cast<float>(value);
    }
    return audio;
}

double amplitudeAt(const Audio& audio, double targetHz, double startSeconds,
                   double endSeconds) {
    const int sampleRate = audio.sampleRate();
    const int64_t start = std::clamp<int64_t>(
        static_cast<int64_t>(startSeconds * sampleRate), 0, audio.numFrames());
    const int64_t end = std::clamp<int64_t>(
        static_cast<int64_t>(endSeconds * sampleRate), start, audio.numFrames());
    const int64_t count = end - start;
    if (count < 64) return 0.0;
    const int fftSize = nextPowerOfTwo(static_cast<int>(count));
    std::vector<std::complex<float>> data(static_cast<size_t>(fftSize));
    for (int64_t i = 0; i < count; ++i) {
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * kPi * static_cast<double>(i) / static_cast<double>(count - 1));
        data[static_cast<size_t>(i)] = {
            static_cast<float>(audio.sample(0, start + i) * window), 0.0f};
    }
    FFT fft(fftSize);
    fft.forward(data);
    size_t peak = static_cast<size_t>(std::llround(
        targetHz * static_cast<double>(fftSize) / sampleRate));
    peak = std::clamp<size_t>(peak, 1, data.size() / 2 - 2);
    float best = 0.0f;
    for (size_t i = peak - 1; i <= peak + 1; ++i)
        best = std::max(best, std::abs(data[i]));
    const double windowSum = static_cast<double>(count) / 2.0;
    return static_cast<double>(best) * 2.0 / windowSum;
}

double windowRms(const Audio& audio, double startSeconds, double endSeconds) {
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

VWTEST("spectrogram has stable dimensions and values") {
    const Audio audio = makeTone(440.0, 0.5, 0.2);
    const Spectrogram spectrogram = computeSpectrogram(audio, 1024, 256);
    CHECK_EQ(spectrogram.fftSize, 1024);
    CHECK_EQ(spectrogram.hopSize, 256);
    CHECK_EQ(spectrogram.binCount, 513);
    CHECK(spectrogram.frameCount > 0);
    CHECK_EQ(spectrogram.magnitudes.size(),
             static_cast<size_t>(spectrogram.frameCount) *
                 static_cast<size_t>(spectrogram.binCount));
    CHECK_EQ(spectrogram.decibels.size(), spectrogram.magnitudes.size());
    CHECK_NEAR(spectrogram.binFrequency(0), 0.0, 1e-9);
    CHECK_NEAR(spectrogram.binFrequency(1), 48000.0 / 1024.0, 1e-6);
    const int expectedBin = static_cast<int>(
        std::llround(440.0 * 1024.0 / 48000.0));
    const int frame = spectrogram.frameCount / 2;
    float peak = 0.0f;
    for (int bin = 1; bin < spectrogram.binCount; ++bin)
        peak = std::max(peak, spectrogram.magnitudeAt(frame, bin));
    CHECK_NEAR(spectrogram.magnitudeAt(frame, expectedBin), peak, 1e-3);
    for (float value : spectrogram.decibels) CHECK(std::isfinite(value));
}

VWTEST("spectral gain boosts only the selected band") {
    const Audio audio = makeMix({440.0, 4000.0}, {0.3, 0.1}, 0.5);
    const Audio boosted = spectralGain(audio, 300.0, 600.0, 12.0);
    CHECK_EQ(boosted.numFrames(), audio.numFrames());
    CHECK_EQ(boosted.sampleRate(), audio.sampleRate());
    const double originalLow = amplitudeAt(audio, 440.0, 0.1, 0.4);
    const double boostedLow = amplitudeAt(boosted, 440.0, 0.1, 0.4);
    const double originalHigh = amplitudeAt(audio, 4000.0, 0.1, 0.4);
    const double boostedHigh = amplitudeAt(boosted, 4000.0, 0.1, 0.4);
    CHECK_NEAR(boostedLow / originalLow, 3.981, 0.12);
    CHECK_NEAR(boostedHigh / originalHigh, 1.0, 0.05);

    const Audio cut = spectralGain(audio, 300.0, 600.0, -12.0);
    const double cutLow = amplitudeAt(cut, 440.0, 0.1, 0.4);
    CHECK_NEAR(cutLow / originalLow, 0.251, 0.02);
}

VWTEST("spectral gain of zero preserves the signal") {
    const Audio audio = makeMix({440.0, 3000.0}, {0.2, 0.1}, 0.4);
    const Audio same = spectralGain(audio, 100.0, 8000.0, 0.0);
    CHECK_EQ(same.numFrames(), audio.numFrames());
    for (int64_t i = 4800; i < 14400; ++i)
        CHECK_NEAR(same.sample(0, i), audio.sample(0, i), 1e-4);
}

VWTEST("spectral lowpass and highpass isolate bands") {
    const Audio audio = makeMix({440.0, 5000.0}, {0.3, 0.2}, 0.5);
    const Audio lowpassed = spectralFilter(audio, "lowpass", 1000.0);
    CHECK_NEAR(amplitudeAt(lowpassed, 440.0, 0.1, 0.4) /
                   amplitudeAt(audio, 440.0, 0.1, 0.4),
               1.0, 0.08);
    CHECK(amplitudeAt(lowpassed, 5000.0, 0.1, 0.4) /
              amplitudeAt(audio, 5000.0, 0.1, 0.4) <
          0.02);

    const Audio highpassed = spectralFilter(audio, "highpass", 2000.0);
    CHECK(amplitudeAt(highpassed, 440.0, 0.1, 0.4) /
              amplitudeAt(audio, 440.0, 0.1, 0.4) <
          0.02);
    CHECK_NEAR(amplitudeAt(highpassed, 5000.0, 0.1, 0.4) /
                   amplitudeAt(audio, 5000.0, 0.1, 0.4),
               1.0, 0.08);
}

VWTEST("spectral gate removes quiet bins") {
    const Audio audio = makeMix({440.0, 6000.0}, {0.5, 0.001}, 0.5);
    const Audio gated = spectralGate(audio, -40.0);
    const double lowRatio = amplitudeAt(gated, 440.0, 0.1, 0.4) /
                            amplitudeAt(audio, 440.0, 0.1, 0.4);
    const double highRatio = amplitudeAt(gated, 6000.0, 0.1, 0.4) /
                             amplitudeAt(audio, 6000.0, 0.1, 0.4);
    CHECK_NEAR(lowRatio, 1.0, 0.1);
    CHECK(highRatio < 0.05);
}

VWTEST("spectral processing respects the requested range") {
    const Audio original = makeTone(440.0, 0.5, 0.4);
    const Audio result = spectralGain(original, 300.0, 600.0, -90.0,
                                      {9600, 14400});
    CHECK_EQ(result.numFrames(), original.numFrames());
    CHECK(windowRms(result, 0.0, 0.15) > 0.2);
    CHECK(windowRms(result, 0.25, 0.35) > 0.2);
    CHECK(windowRms(result, 0.21, 0.29) < 0.03);
}

VWTEST("document spectral edits are non-destructive") {
    const Audio original = makeTone(440.0, 0.5, 0.4);
    Document doc = Document::fromAudio(original);
    doc.setSelection(9600, 14400);
    REQUIRE(doc.spectralGain(300.0, 600.0, -90.0));
    CHECK_EQ(doc.numFrames(), original.numFrames());
    CHECK(windowRms(doc.audio(), 0.0, 0.15) > 0.2);
    CHECK(windowRms(doc.audio(), 0.21, 0.29) < 0.03);
    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
}

VWTEST("spectral gate and filter reject invalid parameters") {
    const Audio audio = makeTone(440.0, 0.5, 0.1);
    CHECK(spectralGain(audio, 600.0, 300.0, 6.0).empty());
    CHECK(spectralFilter(audio, "unknown", 1000.0).empty());
    CHECK(spectralFilter(audio, "lowpass", 0.0).empty());
    Audio edge = spectralGate(audio, -40.0, {9000, 1000});
    CHECK(edge.empty());
}

VWTEST("phase spectrogram tracks tone frequency and phase confidence") {
    const Audio audio = makeTone(440.0, 0.5, 0.5);
    const PhaseSpectrogram phase =
        computePhaseSpectrogram(audio, 2048, 128);
    REQUIRE(phase.frameCount > 4);
    REQUIRE(phase.binCount > 32);
    const int bin = static_cast<int>(std::llround(
        440.0 * phase.fftSize / phase.sampleRate));
    REQUIRE(bin > 0 && bin < phase.binCount);
    const int frame = phase.frameCount / 2;
    CHECK_NEAR(phase.binFrequency(bin), 445.3, 2.0);
    CHECK_NEAR(phase.instantaneousHzAt(frame, bin), 440.0, 10.0);
    CHECK(phase.confidenceAt(frame, bin) > 0.4f);
    CHECK(phase.dbAt(frame, bin) > -30.0f);
    const float value = phase.phaseAt(frame, bin);
    CHECK(value >= -static_cast<float>(kPi));
    CHECK(value <= static_cast<float>(kPi));
}

VWTEST("phase spectrogram confidence falls in silence") {
    const Audio audio = makeTone(440.0, 0.5, 0.4);
    const PhaseSpectrogram phase =
        computePhaseSpectrogram(audio, 1024, 64);
    REQUIRE(phase.valid());
    const int silentBin = phase.binCount * 3 / 4;
    const int frame = phase.frameCount / 2;
    CHECK(phase.confidenceAt(frame, silentBin) < 0.2f);
    CHECK(phase.dbAt(frame, silentBin) < -50.0f);
}

VWTEST("phase spectrogram caps frames and handles empty input") {
    const Audio audio = makeTone(220.0, 0.4, 2.0);
    const PhaseSpectrogram phase =
        computePhaseSpectrogram(audio, 2048, 16);
    REQUIRE(phase.valid());
    CHECK(phase.frameCount <= 16);
    CHECK(phase.frameCount > 0);
    CHECK(phase.hopSize >= 256);
    CHECK_EQ(phase.decibels.size(),
             static_cast<size_t>(phase.frameCount * phase.binCount));
    const PhaseSpectrogram empty =
        computePhaseSpectrogram(Audio{}, 2048, 16);
    CHECK(!empty.valid());
}
