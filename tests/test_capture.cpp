#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Capture.h"
#include "vwave/Processing.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace vwave;

namespace {

Audio makeImpulse(int64_t frames, int sampleRate, int64_t position,
                  float amplitude) {
    Audio audio(1, frames, sampleRate);
    if (position >= 0 && position < frames)
        audio.sample(0, position) = amplitude;
    return audio;
}

} // namespace

VWTEST("sweep generator produces a valid exponential sweep") {
    SweepOptions options;
    options.startHz = 100.0;
    options.endHz = 10000.0;
    options.seconds = 0.1;
    options.sampleRate = 48000;
    options.channels = 2;
    options.amplitude = 0.4;
    const Audio sweep = generateSweep(options);
    CHECK_EQ(sweep.sampleRate(), 48000);
    CHECK_EQ(sweep.numChannels(), 2);
    CHECK_EQ(sweep.numFrames(), int64_t{4800});
    CHECK_NEAR(sweep.peak(), 0.4, 0.01);
    CHECK_NEAR(sweep.sample(0, 0), 0.0, 1e-4);
    CHECK_NEAR(sweep.sample(1, sweep.numFrames() - 1), 0.0, 1e-3);
    for (int64_t i = 0; i < sweep.numFrames(); ++i)
        CHECK(std::isfinite(sweep.sample(0, i)));
}

VWTEST("sweep generator rejects invalid options") {
    SweepOptions options;
    options.seconds = 0.0;
    CHECK(generateSweep(options).empty());
    options.seconds = 1.0;
    options.startHz = 1000.0;
    options.endHz = 100.0;
    CHECK(generateSweep(options).empty());
    options.startHz = 100.0;
    options.endHz = 1000.0;
    options.sampleRate = 0;
    CHECK(generateSweep(options).empty());
    options.sampleRate = 48000;
    options.channels = 0;
    CHECK(generateSweep(options).empty());
}

VWTEST("deconvolution recovers an impulse response") {
    SweepOptions sweepOptions;
    sweepOptions.startHz = 100.0;
    sweepOptions.endHz = 23000.0;
    sweepOptions.seconds = 0.05;
    sweepOptions.sampleRate = 48000;
    sweepOptions.channels = 1;
    sweepOptions.amplitude = 0.5;
    const Audio sweep = generateSweep(sweepOptions);
    const Audio ir = makeImpulse(64, 48000, 7, 0.5f);
    const Audio recorded = convolve(sweep, ir, false);
    REQUIRE(!recorded.empty());

    DeconvolutionOptions options;
    options.irLength = 128;
    options.trimToPeak = false;
    options.normalize = false;
    std::string error;
    const Audio recovered = extractImpulseResponse(recorded, sweep, options,
                                                   &error);
    REQUIRE(!recovered.empty());
    CHECK(error.empty());
    CHECK_EQ(recovered.numFrames(), int64_t{128});
    CHECK_EQ(recovered.sampleRate(), 48000);
    CHECK_NEAR(recovered.sample(0, 7), 0.5, 0.02);
    double otherEnergy = 0.0;
    for (int64_t i = 0; i < recovered.numFrames(); ++i) {
        if (i == 7) continue;
        otherEnergy += static_cast<double>(recovered.sample(0, i)) *
                       recovered.sample(0, i);
    }
    CHECK(otherEnergy < 0.01);
}

VWTEST("deconvolution normalises and aligns at the peak") {
    SweepOptions sweepOptions;
    sweepOptions.startHz = 100.0;
    sweepOptions.endHz = 8000.0;
    sweepOptions.seconds = 0.04;
    sweepOptions.sampleRate = 48000;
    sweepOptions.amplitude = 0.3;
    const Audio sweep = generateSweep(sweepOptions);
    const Audio ir = makeImpulse(32, 48000, 11, 0.2f);
    const Audio recorded = convolve(sweep, ir, false);

    DeconvolutionOptions options;
    options.irLength = 64;
    options.trimToPeak = true;
    options.normalize = true;
    const Audio recovered = extractImpulseResponse(recorded, sweep, options);
    REQUIRE(!recovered.empty());
    CHECK_EQ(recovered.numFrames(), int64_t{64});
    CHECK_NEAR(recovered.sample(0, 0), 1.0, 0.02);
    double peak = 0.0;
    for (int64_t i = 0; i < recovered.numFrames(); ++i)
        peak = std::max(peak, static_cast<double>(std::abs(recovered.sample(0, i))));
    CHECK_NEAR(peak, 1.0, 0.02);
}

VWTEST("deconvolution handles invalid input") {
    Audio empty;
    SweepOptions options;
    options.seconds = 0.01;
    const Audio sweep = generateSweep(options);
    std::string error;
    CHECK(extractImpulseResponse(empty, sweep, {}, &error).empty());
    CHECK(!error.empty());
    CHECK(extractImpulseResponse(sweep, empty, {}, &error).empty());
}

VWTEST("capture manifest writes reproducible metadata") {
    CaptureInfo info;
    info.name = "cabinet";
    info.source = "SM57";
    info.notes = "close mic";
    info.sweep.startHz = 40.0;
    info.sweep.endHz = 16000.0;
    info.sweep.seconds = 2.0;
    info.sampleRate = 48000;
    info.frames = 256;
    TempDirectory directory;
    const std::string path = (directory.path() / "capture.json").string();
    std::string error;
    REQUIRE(writeCaptureManifest(path, info, &error));
    CHECK(error.empty());
    CHECK(std::filesystem::exists(path));
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK(text.find("cabinet") != std::string::npos);
    CHECK(text.find("SM57") != std::string::npos);
    CHECK(text.find("16000") != std::string::npos);
}
