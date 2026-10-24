#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Analysis.h"
#include "vwave/FFT.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
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
