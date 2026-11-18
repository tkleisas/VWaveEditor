#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Recipe.h"
#include "vwave/Xenakis.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace vwave;

namespace {

const char* testPage() {
    return "XENPAGE 1\n"
           "name \"test page\"\n"
           "duration 0.1\n"
           "loop 0 0.1\n"
           "groups 1\n"
           "group 0 0 0 \"Group A\"\n"
           "envelopes 1\n"
           "env 4 0 0 0.01 1 0.05 1 0.1 0\n"
           "tables 1\n"
           "table 48 72 8.175798915643707 12 0 0 0 0\n"
           "arcs 1\n"
           "arc 4 0 0 0 0 0.8 0 2 0 0.5 0.1 0.5\n";
}

double energy(const Audio& audio) {
    double total = 0.0;
    for (int64_t frame = 0; frame < audio.numFrames(); ++frame) {
        const double sample = audio.sample(0, frame);
        total += sample * sample;
    }
    return total;
}

} // namespace

VWTEST("xenakis page renders deterministically") {
    XenakisRenderOptions options;
    options.sampleRate = 8000;
    options.duration = 0.1;
    options.gain = 1.0;
    options.normalize = false;

    std::string error;
    const Audio audio = renderXenakisPage(testPage(), options, &error);
    REQUIRE(error.empty());
    CHECK(!audio.empty());
    CHECK_EQ(audio.sampleRate(), 8000);
    CHECK_EQ(audio.numChannels(), 1);
    CHECK_EQ(audio.numFrames(), int64_t{800});
    CHECK(energy(audio) > 0.0);

    const Audio again = renderXenakisPage(testPage(), options, &error);
    CHECK(error.empty());
    CHECK(again.equals(audio));
}

VWTEST("xenakis page files and recipes regenerate audio") {
    TempDirectory directory;
    const std::filesystem::path path = directory.path() / "test.xenpage";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(static_cast<bool>(out));
        out << testPage();
    }
    CHECK(isXenakisPageFile(path.string()));

    XenakisRenderOptions options;
    options.sampleRate = 8000;
    options.duration = 0.1;
    options.normalize = false;

    std::string error;
    const Audio audio = renderXenakisPageFile(path.string(), options, &error);
    REQUIRE(error.empty());
    CHECK(!audio.empty());
    CHECK_EQ(audio.numFrames(), int64_t{800});

    const nlohmann::json recipe = makeXenakisTrackRecipe(testPage(), options);
    CHECK_EQ(recipe.value("type", std::string{}), std::string{"xenakis_page"});
    Audio regenerated;
    REQUIRE(renderTrackRecipe(recipe, regenerated, &error));
    CHECK(error.empty());
    CHECK(regenerated.equals(audio));
}


VWTEST("xenakis generators produce renderable pages") {
    for (int generator = 0; generator < 4; ++generator) {
        XenakisGenerateOptions options;
        options.generator =
            static_cast<XenakisGenerator>(generator);
        options.duration = 1.0;
        options.lowIndex = 48;
        options.highIndex = 72;
        options.noteDuration = 0.1;
        options.sievePreset = 0;
        options.sieveMode = XenakisSieveMode::Arpeggio;
        options.markovStates = 4;
        options.cloudDensity = 12.0;
        options.strategyRows = 3;
        options.strategyCols = 3;
        options.strategySegment = 0.2;
        options.seed = 7;

        std::string error;
        const std::string page = generateXenakisPage(options, &error);
        CHECK(error.empty());
        CHECK(!page.empty());

        XenakisRenderOptions render;
        render.sampleRate = 8000;
        render.duration = 0.2;
        render.normalize = false;
        const Audio audio = renderXenakisPage(page, render, &error);
        CHECK(error.empty());
        CHECK(!audio.empty());
        CHECK_EQ(audio.numFrames(), int64_t{1600});
        CHECK(energy(audio) > 0.0);
    }
}

VWTEST("xenakis recipes preserve GENDYN options") {
    XenakisRenderOptions options;
    options.sampleRate = 8000;
    options.duration = 0.2;
    options.normalize = false;
    options.gendyn.segments = 7;
    options.gendyn.distribution = XenakisDistribution::Logistic;
    options.gendyn.durationStep = 0.3;
    options.gendyn.durationBarrier = 0.25;
    options.gendyn.ampStep = 0.1;
    options.gendyn.ampBarrier = 0.15;
    options.gendyn.mirror = true;

    const nlohmann::json recipe = makeXenakisTrackRecipe(testPage(), options);
    Audio audio;
    std::string error;
    REQUIRE(renderTrackRecipe(recipe, audio, &error));
    CHECK(error.empty());
    CHECK(!audio.empty());
    CHECK_EQ(audio.numFrames(), int64_t{1600});

    const Audio direct = renderXenakisPage(testPage(), options, &error);
    CHECK(error.empty());
    CHECK(audio.equals(direct));
}

VWTEST("xenakis page rejects malformed input") {
    XenakisRenderOptions options;
    options.sampleRate = 8000;
    std::string error;
    const Audio audio = renderXenakisPage("not a xenpage", options, &error);
    CHECK(audio.empty());
    CHECK(!error.empty());
    CHECK(!isXenakisPageFile("missing.xenpage"));
}
