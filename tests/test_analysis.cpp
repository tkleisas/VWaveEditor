#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

Audio makeClickTrain(double bpm, double seconds, int sampleRate = 48000) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(2, frames, sampleRate);
    const double interval = 60.0 / bpm;
    for (double time = 0.0; time < seconds; time += interval) {
        const int64_t start = static_cast<int64_t>(time * sampleRate);
        for (int64_t i = 0; i < 480; ++i) {
            const double envelope = std::exp(-static_cast<double>(i) / 80.0);
            const double value = envelope * std::sin(
                2.0 * kPi * 1800.0 * static_cast<double>(i) / sampleRate);
            if (start + i < frames) {
                audio.sample(0, start + i) += static_cast<float>(0.8 * value);
                audio.sample(1, start + i) += static_cast<float>(0.8 * value);
            }
        }
    }
    return audio;
}

Audio makeSine(double frequency, double seconds, int sampleRate = 48000) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(1, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(
            0.7 * std::sin(2.0 * kPi * frequency * static_cast<double>(i) /
                           sampleRate));
    }
    return audio;
}

} // namespace

VWTEST("onset detection finds click positions") {
    const Audio clicks = makeClickTrain(120.0, 4.0);
    const std::vector<Onset> onsets = detectOnsets(clicks, 0.5);
    CHECK(onsets.size() >= 7);
    CHECK(onsets.size() <= 9);
    const double expectedInterval = 0.5;
    for (size_t i = 1; i < onsets.size(); ++i) {
        const double interval = static_cast<double>(
            onsets[i].frame - onsets[i - 1].frame) / clicks.sampleRate();
        CHECK_NEAR(interval, expectedInterval, 0.03);
    }
    CHECK(onsets.front().frame < 2000);
}

VWTEST("beat analysis detects 120 bpm") {
    const Audio clicks = makeClickTrain(120.0, 8.0);
    const BeatAnalysis analysis = analyzeBeats(clicks, 0.5);
    CHECK_NEAR(analysis.bpm, 120.0, 1.5);
    CHECK(analysis.confidence > 0.2);
    CHECK(analysis.beats.size() >= 14);
    CHECK(analysis.beats.size() <= 17);
    const double period = 60.0 / 120.0;
    CHECK_NEAR(static_cast<double>(analysis.beats.front()) /
                   clicks.sampleRate(),
               0.0, 0.04);
    for (size_t i = 1; i < analysis.beats.size(); ++i) {
        const double interval = static_cast<double>(
            analysis.beats[i] - analysis.beats[i - 1]) /
            clicks.sampleRate();
        CHECK_NEAR(interval, period, 0.02);
    }
}

VWTEST("beat analysis detects 90 bpm") {
    const Audio clicks = makeClickTrain(90.0, 8.0);
    const BeatAnalysis analysis = analyzeBeats(clicks, 0.5);
    CHECK_NEAR(analysis.bpm, 90.0, 1.5);
    CHECK(analysis.beats.size() >= 11);
}


VWTEST("beat analysis prefers accented beats over subdivisions") {
    const int sampleRate = 48000;
    const int64_t frames = static_cast<int64_t>(8.0 * sampleRate);
    Audio audio(1, frames, sampleRate);
    int step = 0;
    for (double time = 0.0; time < 8.0; time += 0.25, ++step) {
        const int64_t start = static_cast<int64_t>(time * sampleRate);
        const double amplitude = (step % 2 == 0) ? 0.8 : 0.12;
        for (int64_t i = 0; i < 480; ++i) {
            const int64_t index = start + i;
            if (index >= frames) break;
            audio.sample(0, index) += static_cast<float>(
                amplitude * std::exp(-static_cast<double>(i) / 80.0) *
                std::sin(2.0 * 3.14159265358979323846 * 1800.0 *
                         static_cast<double>(i) / sampleRate));
        }
    }
    const BeatAnalysis analysis = analyzeBeats(audio, 0.5);
    CHECK_NEAR(analysis.bpm, 120.0, 2.0);
}

VWTEST("beat analysis handles silence") {
    Audio silence(2, 48000, 48000);
    const BeatAnalysis analysis = analyzeBeats(silence);
    CHECK_NEAR(analysis.bpm, 0.0, 1e-9);
    CHECK(analysis.onsets.empty());
    CHECK(analysis.beats.empty());
    CHECK_NEAR(analysis.confidence, 0.0, 1e-9);
}

VWTEST("pitch detection measures common frequencies") {
    const Audio a4 = makeSine(440.0, 0.5);
    PitchEstimate estimate = detectPitch(a4);
    CHECK_NEAR(estimate.frequency, 440.0, 1.0);
    CHECK(estimate.confidence > 0.5);

    const Audio a2 = makeSine(110.0, 0.5);
    estimate = detectPitch(a2);
    CHECK_NEAR(estimate.frequency, 110.0, 0.5);

    const Audio a5 = makeSine(880.0, 0.5);
    estimate = detectPitch(a5);
    CHECK_NEAR(estimate.frequency, 880.0, 2.0);
}

VWTEST("pitch detection respects range and silence") {
    const Audio a4 = makeSine(440.0, 0.5);
    PitchOptions options;
    options.minHz = 500.0;
    options.maxHz = 1000.0;
    PitchEstimate estimate = detectPitch(a4, {}, options);
    CHECK_NEAR(estimate.frequency, 0.0, 1e-9);

    Audio silence(1, 24000, 48000);
    estimate = detectPitch(silence);
    CHECK_NEAR(estimate.frequency, 0.0, 1e-9);
}

VWTEST("transient slicing produces ordered bounded slices") {
    Audio audio(1, 96000, 48000);
    const std::vector<double> hits = {0.25, 0.75, 1.25};
    for (double hit : hits) {
        const int64_t start = static_cast<int64_t>(hit * 48000.0);
        for (int64_t i = 0; i < 480; ++i) {
            audio.sample(0, start + i) += static_cast<float>(
                std::exp(-static_cast<double>(i) / 70.0) * 0.8);
        }
    }
    const std::vector<Slice> slices = sliceByTransients(audio, 0.5, 0.05);
    CHECK_EQ(slices.size(), hits.size() + 1);
    CHECK_EQ(slices.front().range.start, int64_t{0});
    CHECK_EQ(slices.back().range.end, int64_t{96000});
    for (size_t i = 1; i < slices.size(); ++i) {
        CHECK_EQ(slices[i].range.start, slices[i - 1].range.end);
        CHECK(slices[i].range.length() > 0);
        if (i < slices.size() - 1) {
            CHECK_NEAR(static_cast<double>(slices[i].range.start) / 48000.0,
                       hits[i - 1], 0.03);
        }
    }
    for (const auto& slice : slices) CHECK(!slice.name.empty());
}

VWTEST("beat slicing uses the requested grid") {
    Audio audio(1, 96000, 48000);
    for (int64_t i = 0; i < audio.numFrames(); ++i)
        audio.sample(0, i) = 0.1f;
    const std::vector<Slice> slices = sliceByBeats(audio, 120.0, 1);
    CHECK_EQ(slices.size(), size_t{4});
    CHECK_EQ(slices[0].range.start, int64_t{0});
    CHECK_EQ(slices[0].range.end, int64_t{24000});
    CHECK_EQ(slices[3].range.start, int64_t{72000});
    CHECK_EQ(slices[3].range.end, int64_t{96000});
}

VWTEST("marker slicing clamps and requires monotonic markers") {
    Audio audio(1, 48000, 48000);
    const std::vector<Slice> slices =
        sliceByMarkers(audio, {-10, 0, 10000, 10000, 20000, 60000});
    CHECK_EQ(slices.size(), size_t{3});
    CHECK_EQ(slices[0].range.start, int64_t{0});
    CHECK_EQ(slices[0].range.end, int64_t{10000});
    CHECK_EQ(slices[1].range.start, int64_t{10000});
    CHECK_EQ(slices[1].range.end, int64_t{20000});
    CHECK_EQ(slices[2].range.end, int64_t{48000});
}

VWTEST("slice export writes audio and manifest") {
    Audio audio(2, 48000, 48000);
    for (int64_t i = 0; i < audio.numFrames(); ++i) {
        const float value = static_cast<float>(std::sin(i * 0.01));
        audio.sample(0, i) = value;
        audio.sample(1, i) = -value;
    }
    const std::vector<Slice> slices = {
        {{0, 12000}, "intro"}, {{12000, 24000}, "loop"}, {{24000, 48000}, "tail"}};
    TempDirectory directory;
    SliceExportResult result;
    std::string error;
    SliceExportOptions options;
    options.prefix = "hit";
    options.audio.bitDepth = BitDepth::Float32;
    REQUIRE(exportSlices(audio, slices, directory.string(), options, &result,
                         &error));
    CHECK(error.empty());
    CHECK_EQ(result.files.size(), size_t{3});
    CHECK(!result.manifestPath.empty());
    for (const auto& file : result.files)
        CHECK(std::filesystem::exists(file));
    CHECK(std::filesystem::exists(result.manifestPath));

    std::ifstream manifest(result.manifestPath);
    nlohmann::json json;
    manifest >> json;
    CHECK_EQ(json["slices"].size(), size_t{3});
    CHECK_EQ(json["slices"][1]["name"], std::string("loop"));
    CHECK_EQ(json["sampleRate"], 48000);
}

VWTEST("slice export rejects an empty slice list") {
    Audio audio(1, 100, 48000);
    TempDirectory directory;
    std::string error;
    CHECK(!exportSlices(audio, {}, directory.string(), {}, nullptr, &error));
    CHECK(!error.empty());
}
