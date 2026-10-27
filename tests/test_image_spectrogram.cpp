#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/FFT.h"
#include "vwave/ImageSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

ImageData makeImage(int width, int height) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    image.pixels.assign(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    for (size_t i = 3; i < image.pixels.size(); i += 4)
        image.pixels[i] = 255;
    return image;
}

void setPixel(ImageData& image, int x, int y, int r, int g, int b, int a) {
    const size_t index =
        (static_cast<size_t>(y) * static_cast<size_t>(image.width) +
         static_cast<size_t>(x)) *
        static_cast<size_t>(image.channels);
    image.pixels[index] = static_cast<uint8_t>(r);
    image.pixels[index + 1] = static_cast<uint8_t>(g);
    image.pixels[index + 2] = static_cast<uint8_t>(b);
    image.pixels[index + 3] = static_cast<uint8_t>(a);
}

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
            2.0 * kPi * static_cast<double>(i) /
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
    return static_cast<double>(peak) * sampleRate / fftSize;
}

} // namespace

VWTEST("image loader decodes a binary PPM") {
    TempDirectory directory;
    const std::string path =
        (directory.path() / "tiny.ppm").string();
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(static_cast<bool>(out));
        out << "P6\n2 2\n255\n";
        const unsigned char pixels[12] = {
            255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
        out.write(reinterpret_cast<const char*>(pixels), 12);
    }
    ImageData image;
    std::string error;
    REQUIRE(loadImageFile(path, image, &error));
    CHECK(error.empty());
    CHECK_EQ(image.width, 2);
    CHECK_EQ(image.height, 2);
    CHECK_EQ(image.channels, 4);
    CHECK_EQ(image.at(0, 0, 0), uint8_t{255});
    CHECK_EQ(image.at(1, 0, 1), uint8_t{255});
    CHECK_EQ(image.at(0, 1, 2), uint8_t{255});
    CHECK_EQ(image.at(1, 1, 3), uint8_t{255});
}

VWTEST("image spectrogram maps a bright band to a tone") {
    const int width = 128;
    const int height = 128;
    ImageData image = makeImage(width, height);
    const double targetHz = 1000.0;
    const double minHz = 100.0;
    const double maxHz = 7000.0;
    const double frequencyNorm = (targetHz - minHz) / (maxHz - minHz);
    const int targetRow = static_cast<int>(std::llround(
        (1.0 - frequencyNorm) * static_cast<double>(height - 1)));
    for (int y = targetRow - 2; y <= targetRow + 2; ++y) {
        for (int x = 0; x < width; ++x)
            setPixel(image, x, y, 255, 255, 255, 255);
    }
    ImageSpectrogramOptions options;
    options.fftSize = 1024;
    options.hopSize = 256;
    options.sampleRate = 16000;
    options.duration = 0.5;
    options.minHz = minHz;
    options.maxHz = maxHz;
    options.frequencyScale = ImageFrequencyScale::Linear;
    options.phaseMode = ImagePhaseMode::FastGriffinLim;
    options.iterations = 12;
    options.channels = 1;
    std::string error;
    const ImageSpectrogramResult result =
        imageToSpectrogram(image, options, &error);
    REQUIRE(!result.audio.empty());
    CHECK(error.empty());
    CHECK_EQ(result.audio.sampleRate(), 16000);
    CHECK_EQ(result.audio.numFrames(), int64_t{8000});
    CHECK_EQ(result.audio.numChannels(), 1);
    CHECK(result.audio.peak() <= 0.901f);
    CHECK_NEAR(dominantFrequency(result.audio, 0.08, 0.4), 1000.0, 180.0);
    for (int64_t i = 0; i < result.audio.numFrames(); ++i)
        CHECK(std::isfinite(result.audio.sample(0, i)));
}

VWTEST("image spectrogram supports time maps colors and stereo") {
    ImageData image = makeImage(64, 32);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int value = (x * 255) / std::max(1, image.width - 1);
            setPixel(image, x, y, value, 255 - value, 128, 255);
        }
    }
    ImageSpectrogramOptions options;
    options.fftSize = 512;
    options.hopSize = 128;
    options.sampleRate = 8000;
    options.duration = 0.2;
    options.minHz = 100.0;
    options.maxHz = 3000.0;
    options.timeMapping = ImageTimeMapping::Mirrored;
    options.phaseMode = ImagePhaseMode::Color;
    options.channels = 2;
    options.iterations = 0;
    const ImageSpectrogramResult result = imageToSpectrogram(image, options);
    REQUIRE(!result.audio.empty());
    CHECK_EQ(result.audio.numChannels(), 2);
    CHECK_EQ(result.audio.sampleRate(), 8000);
    CHECK(result.duration > 0.19);
    for (int64_t i = 0; i < result.audio.numFrames(); ++i) {
        CHECK(std::isfinite(result.audio.sample(0, i)));
        CHECK_NEAR(result.audio.sample(1, i), result.audio.sample(0, i),
                   1e-6);
    }
}

VWTEST("image phase and mapping names round-trip") {
    for (ImagePhaseMode mode :
         {ImagePhaseMode::GriffinLim, ImagePhaseMode::FastGriffinLim,
          ImagePhaseMode::PhaseLocked, ImagePhaseMode::Random,
          ImagePhaseMode::Color}) {
        ImagePhaseMode parsed = ImagePhaseMode::Random;
        CHECK(imagePhaseModeFromName(imagePhaseModeName(mode), parsed));
        CHECK(parsed == mode);
    }
    for (ImageFrequencyScale scale :
         {ImageFrequencyScale::Linear, ImageFrequencyScale::Logarithmic}) {
        ImageFrequencyScale parsed = ImageFrequencyScale::Linear;
        CHECK(imageFrequencyScaleFromName(imageFrequencyScaleName(scale),
                                         parsed));
        CHECK(parsed == scale);
    }
    for (ImageTimeMapping mapping :
         {ImageTimeMapping::Direct, ImageTimeMapping::Mirrored,
          ImageTimeMapping::Looped}) {
        ImageTimeMapping parsed = ImageTimeMapping::Direct;
        CHECK(imageTimeMappingFromName(imageTimeMappingName(mapping), parsed));
        CHECK(parsed == mapping);
    }
    ImagePhaseMode invalid = ImagePhaseMode::Random;
    CHECK(!imagePhaseModeFromName("not-a-mode", invalid));

    ImageData image = makeImage(32, 32);
    for (int y = 8; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            setPixel(image, x, y, 255, 255, 255, 255);
    ImageSpectrogramOptions options;
    options.sampleRate = 8000;
    options.duration = 0.1;
    options.fftSize = 256;
    options.hopSize = 64;
    options.phaseMode = ImagePhaseMode::PhaseLocked;
    options.iterations = 2;
    const ImageSpectrogramResult result = imageToSpectrogram(image, options);
    CHECK(!result.audio.empty());
    CHECK(result.audio.peak() <= 0.901f);
}

VWTEST("image spectrogram handles transparent and invalid input") {
    ImageData transparent = makeImage(16, 16);
    for (size_t i = 3; i < transparent.pixels.size(); i += 4)
        transparent.pixels[i] = 0;
    ImageSpectrogramOptions options;
    options.sampleRate = 8000;
    options.duration = 0.1;
    options.fftSize = 256;
    options.hopSize = 64;
    options.phaseMode = ImagePhaseMode::Random;
    const ImageSpectrogramResult silent =
        imageToSpectrogram(transparent, options);
    REQUIRE(!silent.audio.empty());
    CHECK_NEAR(silent.audio.peak(), 0.0, 1e-9);

    ImageData invalid;
    std::string error;
    const ImageSpectrogramResult bad =
        imageToSpectrogram(invalid, options, &error);
    CHECK(bad.audio.empty());
    CHECK(!error.empty());

    ImageData image = makeImage(16, 16);
    ImageSpectrogramOptions badRange = options;
    badRange.minHz = 5000.0;
    badRange.maxHz = 1000.0;
    error.clear();
    const ImageSpectrogramResult badResult =
        imageToSpectrogram(image, badRange, &error);
    CHECK(badResult.audio.empty());
    CHECK(!error.empty());
}

VWTEST("image spectrogram reports progress and supports cancellation") {
    ImageData image = makeImage(64, 64);
    ImageSpectrogramOptions options;
    options.sampleRate = 8000;
    options.duration = 0.5;
    options.fftSize = 256;
    options.hopSize = 64;
    options.phaseMode = ImagePhaseMode::FastGriffinLim;
    options.iterations = 8;
    std::vector<double> progress;
    options.progress = [&](double value) { progress.push_back(value); };
    options.cancelled = [&] { return progress.size() >= 2; };
    std::string error;
    const ImageSpectrogramResult result =
        imageToSpectrogram(image, options, &error);
    CHECK(result.audio.empty());
    CHECK(!error.empty());
    CHECK(progress.size() >= 2);
    for (size_t i = 1; i < progress.size(); ++i)
        CHECK(progress[i] >= progress[i - 1]);
}
