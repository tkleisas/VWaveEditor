#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Analysis.h"
#include "vwave/FFT.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

using namespace vwave;

namespace {

Audio sineAudio(double frequency, double seconds, int sampleRate = 48000) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(1, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(
            0.7 * std::sin(2.0 * 3.14159265358979323846 * frequency *
                           static_cast<double>(i) / sampleRate));
    }
    return audio;
}

double peakBin(const std::vector<float>& frame) {
    int fftSize = nextPowerOfTwo(std::max<int>(64, static_cast<int>(frame.size())));
    std::vector<std::complex<float>> data(static_cast<size_t>(fftSize));
    for (size_t i = 0; i < frame.size(); ++i)
        data[i] = {frame[i], 0.0f};
    FFT fft(fftSize);
    fft.forward(data);
    double best = 0.0;
    size_t bestBin = 0;
    for (size_t i = 1; i < data.size() / 2; ++i) {
        const double magnitude = std::abs(data[i]);
        if (magnitude > best) {
            best = magnitude;
            bestBin = i;
        }
    }
    return static_cast<double>(bestBin);
}

} // namespace

VWTEST("wavetable from harmonics builds a normalised cycle") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 1024, "sine");
    CHECK(table.valid());
    CHECK_EQ(table.frameSize(), 1024);
    CHECK_EQ(table.frameCount(), 1);
    CHECK_EQ(table.name(), std::string("sine"));
    CHECK_NEAR(table.frame(0).size(), size_t{1024}, 0);
    CHECK_NEAR(table.sample(0.0, 0.25), 0.999, 0.002);
    const float peak = *std::max_element(table.frame(0).begin(),
                                         table.frame(0).end());
    CHECK_NEAR(peak, 0.999, 0.002);
}

VWTEST("wavetable frame operations validate sizes") {
    Wavetable table(256, "ops");
    std::vector<float> frame(256, 0.5f);
    REQUIRE(table.addFrame(frame));
    CHECK_EQ(table.frameCount(), 1);
    CHECK(!table.addFrame(std::vector<float>(128, 0.0f)));
    CHECK(!table.setFrame(3, frame));
    frame[0] = -0.5f;
    REQUIRE(table.setFrame(0, frame));
    CHECK_NEAR(table.frame(0)[0], -0.5, 1e-9);
    CHECK(!table.removeFrame(4));
    CHECK(table.removeFrame(0));
    CHECK_EQ(table.frameCount(), 0);
    CHECK(!table.valid());
}

VWTEST("wavetable sample interpolates frames and loops phase") {
    Wavetable table(8, "morph");
    std::vector<float> first(8, 1.0f);
    std::vector<float> second(8, -1.0f);
    REQUIRE(table.addFrame(first));
    REQUIRE(table.addFrame(second));
    CHECK_NEAR(table.sample(0.0, 0.0), 1.0, 1e-6);
    CHECK_NEAR(table.sample(1.0, 0.0), -1.0, 1e-6);
    CHECK_NEAR(table.sample(0.5, 0.0), 0.0, 1e-6);
    CHECK_NEAR(table.sample(0.0, 0.1), 1.0, 1e-6);
    CHECK_NEAR(table.sample(0.0, 0.9), 1.0, 1e-6);
}

VWTEST("wavetable extraction finds a cycle from audio") {
    const Audio audio = sineAudio(440.0, 0.5);
    WavetableExtractOptions options;
    options.frameSize = 1024;
    options.frameCount = 4;
    options.name = "extracted";
    std::string error;
    const Wavetable table = extractWavetable(audio, options, &error);
    REQUIRE(table.valid());
    CHECK(error.empty());
    CHECK_EQ(table.frameSize(), 1024);
    CHECK_EQ(table.frameCount(), 4);
    CHECK_EQ(table.name(), std::string("extracted"));
    for (int frame = 0; frame < table.frameCount(); ++frame) {
        float peak = 0.0f;
        double sum = 0.0;
        for (float value : table.frame(frame)) {
            peak = std::max(peak, std::abs(value));
            sum += static_cast<double>(value) * value;
        }
        CHECK_NEAR(peak, 0.999, 0.002);
        CHECK(std::sqrt(sum / 1024.0) > 0.2);
        CHECK_NEAR(peakBin(table.frame(frame)), 1.0, 1.5);
    }
}

VWTEST("wavetable extraction handles short and invalid input") {
    Audio silence(1, 1000, 48000);
    WavetableExtractOptions options;
    options.frameSize = 256;
    options.frameCount = 2;
    std::string error;
    const Wavetable table = extractWavetable(silence, options, &error);
    CHECK(table.valid());
    CHECK_EQ(table.frameSize(), 256);
    CHECK_EQ(table.frameCount(), 2);

    Audio empty;
    CHECK(!extractWavetable(empty, options, &error).valid());
}

VWTEST("wavetable save and load round-trip") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0, 0.25}, 512,
                                               "roundtrip");
    table.setSampleRate(48000);
    TempDirectory directory;
    const std::string path = (directory.path() / "table.vwt").string();
    std::string error;
    REQUIRE(table.save(path, &error));
    CHECK(error.empty());

    Wavetable loaded;
    REQUIRE(Wavetable::load(path, loaded, &error));
    CHECK_EQ(loaded.frameSize(), 512);
    CHECK_EQ(loaded.frameCount(), 1);
    CHECK_EQ(loaded.name(), std::string("roundtrip"));
    CHECK_EQ(loaded.sampleRate(), 48000);
    for (size_t i = 0; i < table.frame(0).size(); ++i)
        CHECK_NEAR(loaded.frame(0)[i], table.frame(0)[i], 1e-9);
}

VWTEST("wavetable wav export and import round-trip") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0}, 256, "wavetable");
    REQUIRE(table.addFrame(table.frame(0)));
    TempDirectory directory;
    const std::string path = (directory.path() / "table.wav").string();
    std::string error;
    REQUIRE(table.saveWav(path, 44100, &error));
    CHECK(error.empty());
    CHECK(std::filesystem::exists(path));

    Wavetable loaded;
    REQUIRE(Wavetable::loadWav(path, 256, loaded, &error));
    CHECK_EQ(loaded.frameSize(), 256);
    CHECK_EQ(loaded.frameCount(), 2);
    for (int frame = 0; frame < 2; ++frame) {
        for (size_t i = 0; i < 256; ++i)
            CHECK_NEAR(loaded.frame(frame)[i], table.frame(frame)[i], 1e-6);
    }
}

VWTEST("wavetable load rejects invalid files") {
    Wavetable table;
    std::string error;
    CHECK(!Wavetable::load("/definitely/not/a/table.vwt", table, &error));
    CHECK(!error.empty());
}

namespace {

std::vector<float> constantFrame(int size, float value) {
    return std::vector<float>(static_cast<size_t>(size), value);
}

std::vector<float> sineFrame(int size, double phaseOffset = 0.0,
                             int harmonics = 1) {
    std::vector<float> frame(static_cast<size_t>(size), 0.0f);
    for (int i = 0; i < size; ++i) {
        const double phase = 2.0 * 3.14159265358979323846 *
                             static_cast<double>(i) /
                             static_cast<double>(size) + phaseOffset;
        double value = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            value += std::sin(phase * h) / static_cast<double>(h);
        frame[static_cast<size_t>(i)] = static_cast<float>(value);
    }
    return frame;
}

double frameRms(const std::vector<float>& frame) {
    double sum = 0.0;
    for (float value : frame) sum += static_cast<double>(value) * value;
    return frame.empty() ? 0.0 : std::sqrt(sum / frame.size());
}

double frameDot(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t count = std::min(a.size(), b.size());
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i)
        sum += static_cast<double>(a[i]) * b[i];
    return sum;
}

double binMagnitude(const std::vector<float>& frame, int bin) {
    const int fftSize = nextPowerOfTwo(
        std::max<int>(64, static_cast<int>(frame.size())));
    std::vector<std::complex<float>> data(static_cast<size_t>(fftSize));
    for (size_t i = 0; i < frame.size(); ++i)
        data[i] = {frame[i], 0.0f};
    FFT fft(fftSize);
    fft.forward(data);
    if (bin < 0 || bin >= fftSize / 2) return 0.0;
    return std::abs(data[static_cast<size_t>(bin)]);
}

} // namespace

VWTEST("wavetable keyframe positions drive ordered morphing") {
    Wavetable table(8, "positions");
    REQUIRE(table.addKeyframe(constantFrame(8, 1.0f), 0.0));
    REQUIRE(table.addKeyframe(constantFrame(8, 2.0f), 0.25));
    REQUIRE(table.addKeyframe(constantFrame(8, -1.0f), 1.0));
    CHECK_EQ(table.framePositions().size(), size_t{3});
    CHECK_NEAR(table.framePosition(1), 0.25, 1e-12);
    CHECK_NEAR(table.sampleMorph(0.0, 0.0), 1.0, 1e-6);
    CHECK_NEAR(table.sampleMorph(0.125, 0.0), 1.5, 1e-6);
    CHECK_NEAR(table.sampleMorph(0.25, 0.0), 2.0, 1e-6);
    CHECK_NEAR(table.sampleMorph(0.625, 0.0), 0.5, 1e-6);
    CHECK_NEAR(table.sampleMorph(1.0, 0.0), -1.0, 1e-6);
    CHECK(!table.setFramePositions({0.1, 0.2}));
    REQUIRE(table.setFramePosition(1, 0.5));
    CHECK_NEAR(table.sampleMorph(0.25, 0.0), 1.5, 1e-6);
}

VWTEST("wavetable equal power morphing preserves power") {
    Wavetable table(16, "equal");
    REQUIRE(table.addFrame(constantFrame(16, 1.0f)));
    REQUIRE(table.addFrame(constantFrame(16, 1.0f)));
    CHECK_NEAR(table.sampleMorph(0.5, 0.0), 1.0, 1e-6);
    table.setMorphMode(WavetableMorphMode::EqualPower);
    CHECK_NEAR(table.sampleMorph(0.5, 0.0), std::sqrt(2.0), 1e-6);
    CHECK_NEAR(table.sampleMorph(0.0, 0.0), 1.0, 1e-6);
    CHECK_NEAR(table.sampleMorph(1.0, 0.0), 1.0, 1e-6);
}

VWTEST("wavetable phase alignment and phase aligned morphing") {
    Wavetable table(256, "phase");
    const auto reference = sineFrame(256);
    const auto shifted = sineFrame(256, 1.5707963267948966);
    REQUIRE(table.addFrame(reference));
    REQUIRE(table.addFrame(shifted));
    CHECK(std::abs(frameDot(reference, shifted)) < 1.0);
    table.setMorphMode(WavetableMorphMode::PhaseAligned);
    const auto alignedMid = table.morphedFrame(0.5);
    CHECK(frameRms(alignedMid) > 0.6);
    CHECK_NEAR(table.frame(1)[0], shifted[0], 1e-6);

    table.setMorphMode(WavetableMorphMode::Linear);
    CHECK(frameRms(table.morphedFrame(0.5)) < 0.55);
    table.phaseAlignFrames();
    CHECK(table.designOptions().phaseAligned);
    CHECK(frameDot(table.frame(0), table.frame(1)) > 100.0);
}

VWTEST("wavetable spectral and harmonic morphing differ on phase") {
    Wavetable table(256, "spectral");
    const auto positive = sineFrame(256);
    const auto negative = sineFrame(256, 3.14159265358979323846);
    REQUIRE(table.addFrame(positive));
    REQUIRE(table.addFrame(negative));
    table.setMorphMode(WavetableMorphMode::Spectral);
    const auto spectralStart = table.morphedFrame(0.0);
    const auto spectralMid = table.morphedFrame(0.5);
    CHECK_NEAR(frameRms(spectralStart), frameRms(positive), 1e-5);
    CHECK(frameRms(spectralMid) < 0.01);
    table.setMorphMode(WavetableMorphMode::Harmonic);
    const auto harmonicMid = table.morphedFrame(0.5);
    CHECK(frameRms(harmonicMid) > 0.5);
    CHECK_NEAR(table.morphedFrame(1.0)[0], negative[0], 1e-5);
    CHECK_NEAR(table.sampleMorph(0.0, 0.25), 1.0, 1e-4);
    CHECK_NEAR(table.sampleMorph(1.0, 0.25), -1.0, 1e-4);
    table.frame(0)[0] = 2.0f;
    CHECK_NEAR(table.sampleMorph(0.0, 0.0), 2.0, 1e-4);
}

VWTEST("wavetable free blending supports linear and equal power") {
    Wavetable table(32, "blend");
    REQUIRE(table.addFrame(constantFrame(32, 1.0f)));
    REQUIRE(table.addFrame(constantFrame(32, 1.0f)));
    CHECK_NEAR(table.sampleBlend({1.0, 1.0}, 0.0), 2.0, 1e-6);
    CHECK_NEAR(table.sampleBlend({0.25, 0.75}, 0.0), 1.0, 1e-6);
    table.setMorphMode(WavetableMorphMode::EqualPower);
    CHECK_NEAR(table.sampleBlend({1.0, 1.0}, 0.0), std::sqrt(2.0), 1e-6);
    CHECK_NEAR(table.sampleBlend({}, 0.0), 0.0, 1e-6);
    const auto blended = table.blendedFrame({1.0, 1.0});
    CHECK_EQ(blended.size(), size_t{32});
    CHECK_NEAR(blended[0], std::sqrt(2.0), 1e-6);
}

VWTEST("wavetable band limiting and quantization modify frames") {
    Wavetable table(1024, "quality");
    REQUIRE(table.addFrame(constantFrame(1024, 0.0f)));
    auto& frame = table.frame(0);
    for (int i = 0; i < 1024; ++i)
        frame[static_cast<size_t>(i)] = i < 512 ? 0.75f : -0.75f;
    CHECK(binMagnitude(frame, 3) > 0.1);
    table.bandLimitFrames(1);
    CHECK(table.designOptions().bandLimited);
    CHECK_EQ(table.designOptions().maxHarmonic, 1);
    CHECK(binMagnitude(table.frame(0), 3) < 1e-4);
    CHECK(binMagnitude(table.frame(0), 1) > 0.1);

    Wavetable quantized(8, "quantize");
    REQUIRE(quantized.addFrame({0.1f, 0.2f, 0.3f, 0.4f,
                                0.5f, 0.6f, 0.7f, 0.8f}));
    quantized.quantizeFrames(3);
    CHECK_EQ(quantized.designOptions().quantizationBits, 3);
    for (float value : quantized.frame(0)) {
        const double scaled = static_cast<double>(value) * 4.0;
        CHECK_NEAR(scaled, std::round(scaled), 1e-6);
    }
}

VWTEST("wavetable resampling preserves shape and limits frames") {
    Wavetable table(1024, "resample");
    REQUIRE(table.addFrame(sineFrame(1024)));
    REQUIRE(table.addFrame(sineFrame(1024, 0.5)));
    REQUIRE(table.addFrame(sineFrame(1024, 1.0)));
    REQUIRE(table.addFrame(sineFrame(1024, 1.5)));
    const Wavetable small = table.resampled(256);
    CHECK_EQ(small.frameSize(), 256);
    CHECK_EQ(small.frameCount(), 4);
    CHECK_NEAR(small.sampleMorph(0.0, 0.25), 1.0, 0.02);
    const Wavetable limited = table.resampled(256, 2);
    CHECK_EQ(limited.frameCount(), 2);
    CHECK_NEAR(limited.framePosition(0), 0.0, 1e-12);
    CHECK_NEAR(limited.framePosition(1), 1.0, 1e-12);
    const Wavetable single = table.resampled(256, 1);
    CHECK_EQ(single.frameCount(), 1);
}

VWTEST("wavetable loads legacy v1 files without metadata") {
    Wavetable table(64, "legacy");
    REQUIRE(table.addFrame(sineFrame(64)));
    TempDirectory directory;
    const std::string path = (directory.path() / "legacy.vwt").string();
    std::string error;
    REQUIRE(table.save(path, &error));
    nlohmann::json root;
    {
        std::ifstream in(path);
        REQUIRE(static_cast<bool>(in));
        in >> root;
    }
    root["version"] = 1;
    root.erase("rootNote");
    root.erase("morphMode");
    root.erase("interpolation");
    root.erase("keyframePositions");
    root.erase("oversampling");
    root.erase("bandLimited");
    root.erase("maxHarmonic");
    root.erase("quantizationBits");
    root.erase("normalized");
    root.erase("dcRemoved");
    root.erase("phaseAligned");
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(static_cast<bool>(out));
        out << root.dump();
    }
    Wavetable loaded;
    REQUIRE(Wavetable::load(path, loaded, &error));
    CHECK_EQ(loaded.frameCount(), 1);
    CHECK(loaded.morphMode() == WavetableMorphMode::Linear);
    CHECK_NEAR(loaded.frame(0)[0], table.frame(0)[0], 1e-9);
}

VWTEST("wavetable v2 persistence keeps design metadata") {
    Wavetable table(128, "v2");
    REQUIRE(table.addKeyframe(sineFrame(128), 0.0));
    REQUIRE(table.addKeyframe(sineFrame(128, 0.5), 0.4));
    REQUIRE(table.addKeyframe(sineFrame(128, 1.0), 1.0));
    table.setMorphMode(WavetableMorphMode::Harmonic);
    table.setInterpolation(WavetableInterpolation::Cubic);
    table.setRootNote(57.0);
    table.setOversampling(4);
    table.setSampleRate(44100);
    WavetableDesignOptions options = table.designOptions();
    options.bandLimited = true;
    options.maxHarmonic = 12;
    options.quantizationBits = 5;
    options.normalized = true;
    options.dcRemoved = true;
    table.setDesignOptions(options);

    TempDirectory directory;
    const std::string path = (directory.path() / "design.vwt").string();
    std::string error;
    REQUIRE(table.save(path, &error));
    Wavetable loaded;
    REQUIRE(Wavetable::load(path, loaded, &error));
    CHECK(loaded.morphMode() == WavetableMorphMode::Harmonic);
    CHECK(loaded.interpolation() == WavetableInterpolation::Cubic);
    CHECK_NEAR(loaded.rootNote(), 57.0, 1e-12);
    CHECK_EQ(loaded.oversampling(), 4);
    CHECK_EQ(loaded.designOptions().maxHarmonic, 12);
    CHECK_EQ(loaded.designOptions().quantizationBits, 5);
    CHECK(loaded.designOptions().bandLimited);
    CHECK(loaded.designOptions().normalized);
    CHECK(loaded.designOptions().dcRemoved);
    REQUIRE(loaded.framePositions().size() == size_t{3});
    CHECK_NEAR(loaded.framePosition(1), 0.4, 1e-12);
    for (size_t i = 0; i < table.frame(1).size(); ++i)
        CHECK_NEAR(loaded.frame(1)[i], table.frame(1)[i], 1e-9);
}

VWTEST("wavetable wav sidecar preserves v2 metadata and loadAuto") {
    Wavetable table(64, "wavmeta");
    REQUIRE(table.addKeyframe(constantFrame(64, 0.25f), 0.0));
    REQUIRE(table.addKeyframe(constantFrame(64, 0.5f), 1.0));
    table.setMorphMode(WavetableMorphMode::EqualPower);
    table.setRootNote(48.0);
    TempDirectory directory;
    const std::string path = (directory.path() / "meta.wav").string();
    std::string error;
    REQUIRE(table.saveWav(path, 48000, &error));
    CHECK(std::filesystem::exists(path + ".json"));
    Wavetable loaded;
    REQUIRE(Wavetable::loadAuto(path, loaded, &error));
    CHECK(loaded.morphMode() == WavetableMorphMode::EqualPower);
    CHECK_NEAR(loaded.rootNote(), 48.0, 1e-12);
    CHECK_EQ(loaded.frameCount(), 2);
    CHECK_NEAR(loaded.framePosition(1), 1.0, 1e-12);
    CHECK_NEAR(loaded.frame(0)[0], 0.25, 1e-6);
}
