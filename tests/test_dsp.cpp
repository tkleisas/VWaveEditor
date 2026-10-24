#include "test_framework.h"

#include "vwave/Audio.h"
#include "vwave/Document.h"
#include "vwave/FFT.h"
#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

Audio makeSine(double frequency, double seconds, int sampleRate,
               int channels = 2) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(channels, frames, sampleRate);
    for (int ch = 0; ch < channels; ++ch) {
        for (int64_t i = 0; i < frames; ++i) {
            audio.sample(ch, i) = static_cast<float>(
                0.6 * std::sin(2.0 * kPi * frequency * static_cast<double>(i) /
                               sampleRate));
        }
    }
    return audio;
}

Audio makeImpulse(int64_t frames, int sampleRate, int64_t position,
                  float amplitude = 1.0f) {
    Audio audio(1, frames, sampleRate);
    if (position >= 0 && position < frames) audio.sample(0, position) = amplitude;
    return audio;
}

double dominantFrequency(const Audio& audio, int channel, double startSeconds,
                         double endSeconds) {
    const int sampleRate = audio.sampleRate();
    const int64_t start = std::clamp<int64_t>(
        static_cast<int64_t>(startSeconds * sampleRate), 0, audio.numFrames());
    const int64_t end = std::clamp<int64_t>(
        static_cast<int64_t>(endSeconds * sampleRate), start, audio.numFrames());
    const int64_t count = end - start;
    if (count < 16) return 0.0;
    int fftSize = nextPowerOfTwo(static_cast<int>(count));
    std::vector<std::complex<float>> data(static_cast<size_t>(fftSize));
    for (int64_t i = 0; i < count; ++i) {
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * kPi * static_cast<double>(i) / static_cast<double>(count - 1));
        data[static_cast<size_t>(i)] = {
            static_cast<float>(audio.sample(channel, start + i) * window), 0.0f};
    }
    FFT fft(fftSize);
    fft.forward(data);
    size_t peak = 1;
    float peakMagnitude = 0.0f;
    for (size_t i = 1; i < data.size() / 2; ++i) {
        const float magnitude = std::abs(data[i]);
        if (magnitude > peakMagnitude) {
            peakMagnitude = magnitude;
            peak = i;
        }
    }
    double corrected = static_cast<double>(peak);
    if (peak > 0 && peak + 1 < data.size()) {
        const double a = std::abs(data[peak - 1]);
        const double b = std::abs(data[peak]);
        const double c = std::abs(data[peak + 1]);
        const double denominator = a - 2.0 * b + c;
        if (std::abs(denominator) > 1e-12)
            corrected += 0.5 * (a - c) / denominator;
    }
    return corrected * sampleRate / fftSize;
}

double blockRms(const Audio& audio, int channel, double startSeconds,
                double endSeconds) {
    const int sampleRate = audio.sampleRate();
    const int64_t start = std::clamp<int64_t>(
        static_cast<int64_t>(startSeconds * sampleRate), 0, audio.numFrames());
    const int64_t end = std::clamp<int64_t>(
        static_cast<int64_t>(endSeconds * sampleRate), start, audio.numFrames());
    if (end <= start) return 0.0;
    double sum = 0.0;
    for (int64_t i = start; i < end; ++i) {
        const double value = audio.sample(channel, i);
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(end - start));
}

} // namespace

VWTEST("resample preserves duration and frequency") {
    const Audio input = makeSine(440.0, 0.25, 48000);
    const Audio output = resample(input, 24000);
    CHECK_EQ(output.sampleRate(), 24000);
    CHECK_EQ(output.numChannels(), 2);
    CHECK_EQ(output.numFrames(), int64_t{6000});
    CHECK_NEAR(dominantFrequency(output, 0, 0.05, 0.2), 440.0, 2.0);
    CHECK_NEAR(blockRms(output, 0, 0.05, 0.2), 0.6 / std::sqrt(2.0), 0.02);
}

VWTEST("resample upsamples to the expected length") {
    const Audio input = makeSine(220.0, 0.1, 24000);
    const Audio output = resample(input, 48000);
    CHECK_EQ(output.sampleRate(), 48000);
    CHECK_EQ(output.numFrames(), int64_t{4800});
    CHECK_NEAR(dominantFrequency(output, 1, 0.02, 0.08), 220.0, 1.5);
}

VWTEST("resample handles silence and empty audio") {
    Audio silence(1, 1000, 48000);
    Audio output = resample(silence, 24000);
    CHECK_EQ(output.numFrames(), int64_t{500});
    CHECK_NEAR(output.peak(), 0.0, 0.0);

    Audio empty;
    CHECK(resample(empty, 48000).empty());
    CHECK(resample(silence, 0).empty());
}

VWTEST("resample by ratio produces exact requested duration") {
    Audio input = makeSine(330.0, 0.1, 48000);
    Audio doubleLength = resampleByRatio(input, 2.0);
    CHECK_EQ(doubleLength.numFrames(), int64_t{9600});
    Audio halfLength = resampleByRatio(input, 0.5);
    CHECK_EQ(halfLength.numFrames(), int64_t{2400});
}

VWTEST("wsola time stretch has exact duration and preserves pitch") {
    const Audio input = makeSine(440.0, 0.2, 48000);
    StretchOptions options;
    const Audio doubled = timeStretch(input, 2.0, StretchAlgorithm::Wsola,
                                      options);
    CHECK_EQ(doubled.numFrames(), int64_t{19200});
    CHECK_NEAR(dominantFrequency(doubled, 0, 0.05, 0.35), 440.0, 6.0);
    CHECK_NEAR(blockRms(doubled, 0, 0.05, 0.35), 0.6 / std::sqrt(2.0), 0.08);

    const Audio halved = timeStretch(input, 0.5, StretchAlgorithm::Wsola,
                                     options);
    CHECK_EQ(halved.numFrames(), int64_t{4800});
    CHECK_NEAR(dominantFrequency(halved, 0, 0.03, 0.17), 440.0, 8.0);
}

VWTEST("phase vocoder time stretch has exact duration and preserves pitch") {
    const Audio input = makeSine(440.0, 0.2, 48000);
    StretchOptions options;
    const Audio output = timeStretch(input, 2.0,
                                     StretchAlgorithm::PhaseVocoder, options);
    CHECK_EQ(output.numFrames(), int64_t{19200});
    CHECK_NEAR(dominantFrequency(output, 1, 0.05, 0.35), 440.0, 8.0);
    CHECK(blockRms(output, 1, 0.05, 0.35) > 0.2);
}

VWTEST("paulstretch has exact duration and remains finite") {
    const Audio input = makeSine(220.0, 0.1, 48000);
    StretchOptions options;
    const Audio output = paulStretch(input, 4.0, options);
    CHECK_EQ(output.numFrames(), int64_t{19200});
    CHECK_NEAR(dominantFrequency(output, 0, 0.05, 0.35), 220.0, 6.0);
    for (int64_t i = 0; i < output.numFrames(); ++i) {
        REQUIRE(std::isfinite(output.sample(0, i)));
        REQUIRE(std::isfinite(output.sample(1, i)));
    }
    CHECK(blockRms(output, 0, 0.05, 0.35) > 0.15);
}

VWTEST("pitch shift preserves duration and changes pitch") {
    const Audio input = makeSine(440.0, 0.2, 48000);
    StretchOptions options;
    const Audio up = pitchShift(input, 12.0, StretchAlgorithm::PhaseVocoder,
                                options);
    CHECK_EQ(up.numFrames(), input.numFrames());
    CHECK_NEAR(dominantFrequency(up, 0, 0.05, 0.15), 880.0, 15.0);

    const Audio down = pitchShift(input, -12.0, StretchAlgorithm::PhaseVocoder,
                                  options);
    CHECK_EQ(down.numFrames(), input.numFrames());
    CHECK_NEAR(dominantFrequency(down, 0, 0.05, 0.15), 220.0, 8.0);
}

VWTEST("time stretch rejects invalid ratios") {
    const Audio input = makeSine(440.0, 0.01, 48000);
    CHECK(timeStretch(input, 0.0, StretchAlgorithm::Wsola).empty());
    CHECK(timeStretch(input, -1.0, StretchAlgorithm::Wsola).empty());
    Audio empty;
    CHECK(timeStretch(empty, 2.0, StretchAlgorithm::Wsola).empty());
}

VWTEST("convolution finds an impulse response delay") {
    Audio input = makeImpulse(64, 48000, 10);
    Audio ir = makeImpulse(16, 48000, 4, 0.5f);
    Audio output = convolve(input, ir, false);
    CHECK_EQ(output.numFrames(), int64_t{79});
    CHECK_NEAR(output.sample(0, 14), 0.5, 1e-6);
    CHECK_NEAR(output.sample(0, 10), 0.0, 1e-6);
    CHECK_NEAR(output.sample(0, 15), 0.0, 1e-6);
}

VWTEST("document time stretch changes length non-destructively") {
    Audio original(2, 1000, 48000);
    for (int64_t i = 0; i < 1000; ++i) {
        original.sample(0, i) = static_cast<float>(i) / 1000.0f;
        original.sample(1, i) = original.sample(0, i);
    }
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 300);
    REQUIRE(doc.timeStretch(2.0, StretchAlgorithm::Wsola));
    CHECK_EQ(doc.numFrames(), int64_t{1200});
    CHECK_EQ(doc.source().numFrames(), int64_t{1000});
    CHECK(doc.selection().active);
    CHECK_EQ(doc.selection().start, int64_t{100});
    CHECK_EQ(doc.selection().end, int64_t{500});

    REQUIRE(doc.undo());
    CHECK_EQ(doc.numFrames(), int64_t{1000});
    CHECK(doc.audio().equals(original));
}

VWTEST("document pitch shift preserves duration") {
    const Audio original = makeSine(440.0, 0.1, 48000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(500, 2500);
    REQUIRE(doc.pitchShift(12.0, StretchAlgorithm::PhaseVocoder));
    CHECK_EQ(doc.numFrames(), original.numFrames());
    CHECK_EQ(doc.source().numFrames(), original.numFrames());
    CHECK_NEAR(dominantFrequency(doc.audio(), 0, 0.02, 0.04), 880.0, 25.0);
    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
}

VWTEST("in-place stretch preserves unselected prefix and suffix") {
    Audio original(2, 1000, 48000);
    for (int64_t i = 0; i < 1000; ++i) {
        const float value = i < 200 ? 0.1f
                          : (i < 500 ? static_cast<float>(
                                           0.3 * std::sin(2.0 * kPi * 440.0 *
                                                          static_cast<double>(i) /
                                                         48000.0))
                                     : 0.2f);
        original.sample(0, i) = value;
        original.sample(1, i) = value;
    }
    Document doc = Document::fromAudio(original);
    doc.setSelection(200, 500);
    REQUIRE(doc.timeStretch(2.0, StretchAlgorithm::Wsola));
    CHECK_EQ(doc.numFrames(), int64_t{1300});
    CHECK_EQ(doc.selection().start, int64_t{200});
    CHECK_EQ(doc.selection().end, int64_t{800});
    for (int64_t i = 0; i < 200; ++i) {
        CHECK_NEAR(doc.audio().sample(0, i), original.sample(0, i), 1e-9);
        CHECK_NEAR(doc.audio().sample(1, i), original.sample(1, i), 1e-9);
    }
    for (int64_t i = 0; i < 500; ++i) {
        CHECK_NEAR(doc.audio().sample(0, 800 + i),
                   original.sample(0, 500 + i), 1e-9);
        CHECK_NEAR(doc.audio().sample(1, 800 + i),
                   original.sample(1, 500 + i), 1e-9);
    }
}

VWTEST("document resample changes rate and duration") {
    const Audio original = makeSine(440.0, 0.1, 48000);
    Document doc = Document::fromAudio(original);
    REQUIRE(doc.resample(24000));
    CHECK_EQ(doc.sampleRate(), 24000);
    CHECK_EQ(doc.numFrames(), int64_t{2400});
    CHECK_EQ(doc.source().sampleRate(), 48000);
    CHECK_NEAR(doc.duration(), 0.1, 1e-6);
    REQUIRE(doc.undo());
    CHECK_EQ(doc.sampleRate(), 48000);
    CHECK_EQ(doc.numFrames(), int64_t{4800});
    CHECK(doc.audio().equals(original));
}
