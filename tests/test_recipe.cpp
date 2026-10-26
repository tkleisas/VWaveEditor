#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Recipe.h"

#include <filesystem>
#include <fstream>

using namespace vwave;

namespace {

std::string writePpm(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(static_cast<bool>(out));
    const int width = 16;
    const int height = 16;
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char value = (y >= 6 && y <= 9) ? 255 : 0;
            const unsigned char pixel[3] = {value, value, value};
            out.write(reinterpret_cast<const char*>(pixel), 3);
        }
    }
    return path.string();
}

} // namespace

VWTEST("image track recipes regenerate audio") {
    TempDirectory directory;
    const std::string imagePath = writePpm(directory.path() / "image.ppm");
    ImageSpectrogramOptions options;
    options.sampleRate = 8000;
    options.fftSize = 256;
    options.hopSize = 64;
    options.duration = 0.1;
    options.minHz = 100.0;
    options.maxHz = 3000.0;
    options.phaseMode = ImagePhaseMode::Random;
    options.seed = 7;
    const nlohmann::json recipe = makeImageTrackRecipe(imagePath, options);
    Audio audio;
    std::string error;
    REQUIRE(renderTrackRecipe(recipe, audio, &error));
    CHECK(error.empty());
    CHECK(!audio.empty());
    CHECK_EQ(audio.sampleRate(), 8000);
    CHECK_EQ(audio.numFrames(), int64_t{800});

    Audio again;
    REQUIRE(renderTrackRecipe(recipe, again, &error));
    CHECK(again.equals(audio));
}

VWTEST("scope track recipes regenerate audio") {
    const ScopePath path = makeScopeLissajous(3.0, 2.0, 0.0, 0.8, 64);
    ScopeRenderOptions options;
    options.sampleRate = 8000;
    options.duration = 0.1;
    options.scanHz = 100.0;
    options.bandLimited = false;
    const nlohmann::json recipe = makeScopeTrackRecipe(path, options);
    Audio audio;
    std::string error;
    REQUIRE(renderTrackRecipe(recipe, audio, &error));
    CHECK(error.empty());
    CHECK_EQ(audio.numChannels(), 2);
    CHECK_EQ(audio.numFrames(), int64_t{800});
    Audio again;
    REQUIRE(renderTrackRecipe(recipe, again, &error));
    CHECK(again.equals(audio));
}

VWTEST("synth track recipes regenerate audio") {
    Wavetable table = Wavetable::fromHarmonics({0.0, 1.0, 0.5}, 256);
    TempDirectory directory;
    const std::string tablePath =
        (directory.path() / "table.vwt").string();
    std::string error;
    REQUIRE(table.save(tablePath, &error));
    SynthNoteOptions options;
    options.sampleRate = 8000;
    options.channels = 1;
    options.rootNote = 60.0;
    options.envelope.release = 0.01;
    const nlohmann::json recipe =
        makeSynthTrackRecipe(tablePath, 60.0, 0.1, options);
    Audio audio;
    REQUIRE(renderTrackRecipe(recipe, audio, &error));
    CHECK(!audio.empty());
    CHECK_EQ(audio.sampleRate(), 8000);
    CHECK_EQ(audio.numFrames(), int64_t{800});
}

VWTEST("track recipe regeneration reports unsupported recipes") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("x", Audio(1, 100, 8000), 0));
    timeline.track(0).recipe = {{"type", "unknown"}, {"version", 1}};
    Audio audio;
    std::string error;
    CHECK(!regenerateTrack(timeline, 0, audio, &error));
    CHECK(!error.empty());
    timeline.track(0).recipe = {};
    CHECK(!regenerateTrack(timeline, 0, audio, &error));
    CHECK(!regenerateTrack(timeline, 5, audio, &error));
}
