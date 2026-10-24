#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/AudioIO.h"

#include <cmath>
#include <vector>

using namespace vwave;

namespace {

Audio makeStereoRamp(int64_t frames, int sampleRate = 48000) {
    Audio audio(2, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(i) / static_cast<float>(frames);
        audio.sample(1, i) = -static_cast<float>(i) / static_cast<float>(frames);
    }
    return audio;
}

} // namespace

VWTEST("audio construction and planar access") {
    Audio audio(2, 16, 44100);
    CHECK_EQ(audio.numChannels(), 2);
    CHECK_EQ(audio.numFrames(), int64_t{16});
    CHECK_EQ(audio.sampleRate(), 44100);
    CHECK_EQ(audio.totalSamples(), size_t{32});
    CHECK(audio.channel(0) != audio.channel(1));
    CHECK_NEAR(audio.duration(), 16.0 / 44100.0, 1e-12);

    audio.sample(0, 3) = 0.25f;
    audio.sample(1, 3) = -0.5f;
    CHECK_NEAR(audio.sample(0, 3), 0.25, 1e-9);
    CHECK_NEAR(audio.sample(1, 3), -0.5, 1e-9);
}

VWTEST("audio interleaved round-trip") {
    const std::vector<float> interleaved = {1, 2, 3, 4, 5, 6};
    Audio audio = Audio::fromInterleaved(interleaved.data(), 2, 3, 48000);
    CHECK_NEAR(audio.sample(0, 0), 1, 1e-9);
    CHECK_NEAR(audio.sample(1, 0), 2, 1e-9);
    CHECK_NEAR(audio.sample(0, 2), 5, 1e-9);
    CHECK_NEAR(audio.sample(1, 2), 6, 1e-9);
    CHECK(audio.toInterleaved() == interleaved);
}

VWTEST("audio slices clamp and preserve channels") {
    Audio audio = makeStereoRamp(100);
    Audio slice = audio.slice({25, 35});
    CHECK_EQ(slice.numFrames(), int64_t{10});
    CHECK_EQ(slice.numChannels(), 2);
    CHECK_NEAR(slice.sample(0, 0), audio.sample(0, 25), 1e-9);
    CHECK_NEAR(slice.sample(1, 9), audio.sample(1, 34), 1e-9);

    Audio clamped = audio.slice({90, 150});
    CHECK_EQ(clamped.numFrames(), int64_t{10});

    Audio reversedRange = audio.slice({35, 25});
    CHECK_EQ(reversedRange.numFrames(), int64_t{0});
}

VWTEST("audio peak rms and equality") {
    Audio audio(1, 1000, 48000);
    for (int64_t i = 0; i < 1000; ++i) {
        audio.sample(0, i) = static_cast<float>(std::sin(
            2.0 * 3.14159265358979323846 * 480.0 * static_cast<double>(i) / 48000.0));
    }
    CHECK_NEAR(audio.peak(), 1.0, 0.01);
    CHECK_NEAR(audio.rms(), 0.7071, 0.01);
    CHECK(audio.equals(audio));
    Audio copy = audio;
    copy.sample(0, 0) += 0.1f;
    CHECK(!audio.equals(copy));
}

VWTEST("audio file round-trip preserves sample data") {
    Audio original = makeStereoRamp(2400, 48000);
    TempPath file(".wav");
    std::string error;

    AudioExportOptions options;
    options.format = AudioFormat::Wav;
    options.bitDepth = BitDepth::Float32;
    REQUIRE(writeAudio(file.string(), original, options, &error));
    CHECK(error.empty());

    Audio loaded;
    REQUIRE(readAudio(file.string(), loaded, &error));
    CHECK_EQ(loaded.sampleRate(), 48000);
    CHECK_EQ(loaded.numChannels(), 2);
    CHECK_EQ(loaded.numFrames(), int64_t{2400});
    for (int64_t i = 0; i < original.numFrames(); ++i) {
        CHECK_NEAR(loaded.sample(0, i), original.sample(0, i), 1e-6);
        CHECK_NEAR(loaded.sample(1, i), original.sample(1, i), 1e-6);
    }
}

VWTEST("audio probe reports format and dimensions") {
    Audio original = makeStereoRamp(512, 44100);
    TempPath file(".wav");
    std::string error;
    REQUIRE(writeAudio(file.string(), original, {}, &error));

    AudioFileInfo info;
    REQUIRE(probeAudio(file.string(), info, &error));
    CHECK_EQ(info.sampleRate, 44100);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.frames, int64_t{512});
    CHECK(!info.format.empty());
}

VWTEST("audio io reports missing files") {
    Audio audio;
    std::string error;
    CHECK(!readAudio("/definitely/not/a/real/file.wav", audio, &error));
    CHECK(!error.empty());
}

VWTEST("audio int24 round-trip is within quantisation error") {
    Audio original(1, 257, 48000);
    for (int64_t i = 0; i < original.numFrames(); ++i)
        original.sample(0, i) = static_cast<float>(i) / 257.0f;

    TempPath file(".wav");
    AudioExportOptions options;
    options.bitDepth = BitDepth::Int24;
    std::string error;
    REQUIRE(writeAudio(file.string(), original, options, &error));

    Audio loaded;
    REQUIRE(readAudio(file.string(), loaded, &error));
    CHECK_EQ(loaded.numFrames(), original.numFrames());
    for (int64_t i = 0; i < original.numFrames(); ++i)
        CHECK_NEAR(loaded.sample(0, i), original.sample(0, i), 1e-5);
}
