#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Mastering.h"
#include "vwave/Timeline.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

Audio tone(double frequency, double seconds, double amplitude = 0.3,
           int sampleRate = 48000, int channels = 1, double phase = 0.0) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(channels, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        const float value = static_cast<float>(
            amplitude * std::sin(2.0 * kPi * frequency *
                                 static_cast<double>(i) / sampleRate +
                                 phase));
        for (int ch = 0; ch < channels; ++ch)
            audio.sample(ch, i) = ch == 1 ? -value : value;
    }
    return audio;
}

int bandFor(const MasteringAnalysis& analysis, double hz) {
    for (int i = 0; i < analysis.bandCount; ++i) {
        if (hz >= analysis.bands[static_cast<size_t>(i)].lowHz &&
            hz <= analysis.bands[static_cast<size_t>(i)].highHz)
            return i;
    }
    return -1;
}

Timeline makeTimeline() {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("bass-a", tone(100.0, 0.5, 0.3), 0,
                                   &error));
    REQUIRE(timeline.addAudioTrack("bass-b", tone(100.0, 0.5, 0.2), 0,
                                   &error));
    REQUIRE(timeline.addAudioTrack("lead", tone(1000.0, 0.5, 0.25), 0,
                                   &error));
    return timeline;
}

} // namespace

VWTEST("mastering analysis reports stems bands and overlap suggestions") {
    const Timeline timeline = makeTimeline();
    MasteringOptions options;
    options.fftSize = 2048;
    options.hopSize = 1024;
    options.bandCount = 16;
    options.profile = MasteringProfile::Balanced;
    std::string error;
    const MasteringAnalysis analysis =
        analyzeMastering(timeline, options, &error);
    CHECK(error.empty());
    REQUIRE(analysis.valid);
    CHECK_EQ(analysis.sampleRate, 48000);
    CHECK_EQ(analysis.stems.size(), size_t{3});
    CHECK_EQ(analysis.bands.size(), size_t{16});
    CHECK(analysis.summary.find("3 stems") != std::string::npos);
    CHECK(analysis.mixTruePeakDb > -120.0);
    CHECK(analysis.phaseCoherence >= 0.0);
    CHECK(analysis.phaseCoherence <= 1.0);

    const int bassBand = bandFor(analysis, 100.0);
    REQUIRE(bassBand >= 0);
    const auto& band = analysis.bands[static_cast<size_t>(bassBand)];
    CHECK(band.maskingDb > 0.1);
    const auto& first = analysis.stems[0].bands[static_cast<size_t>(bassBand)];
    const auto& second = analysis.stems[1].bands[static_cast<size_t>(bassBand)];
    CHECK(first.dominant != second.dominant);
    CHECK(first.powerDb > -40.0);
    CHECK(second.powerDb > -40.0);
    CHECK(first.suggestedGainDb <= 0.0);
    CHECK(second.suggestedGainDb <= 0.0);
    CHECK(std::abs(first.suggestedGainDb) >= 0.0);
}

VWTEST("mastering profiles change overlap strength and respect limits") {
    const Timeline timeline = makeTimeline();
    const int bassBand = [&] {
        MasteringOptions options;
        options.bandCount = 16;
        const auto analysis = analyzeMastering(timeline, options);
        return bandFor(analysis, 100.0);
    }();
    REQUIRE(bassBand >= 0);

    MasteringOptions definition;
    definition.bandCount = 16;
    definition.profile = MasteringProfile::Definition;
    MasteringOptions separation;
    separation.bandCount = 16;
    separation.profile = MasteringProfile::Separation;
    const auto definitionAnalysis = analyzeMastering(timeline, definition);
    const auto separationAnalysis = analyzeMastering(timeline, separation);
    double definitionCut = 0.0;
    double separationCut = 0.0;
    for (const auto& stem : definitionAnalysis.stems)
        definitionCut = std::max(
            definitionCut,
            std::abs(stem.bands[static_cast<size_t>(bassBand)]
                         .suggestedGainDb));
    for (const auto& stem : separationAnalysis.stems)
        separationCut = std::max(
            separationCut,
            std::abs(stem.bands[static_cast<size_t>(bassBand)]
                         .suggestedGainDb));
    CHECK(separationCut >= definitionCut);

    MasteringOptions limited;
    limited.bandCount = 16;
    limited.profile = MasteringProfile::Separation;
    limited.maxCutDb = 2.0;
    const auto limitedAnalysis = analyzeMastering(timeline, limited);
    for (const auto& stem : limitedAnalysis.stems) {
        for (const auto& band : stem.bands)
            CHECK(band.suggestedGainDb >= -2.0001);
    }

    MasteringOptions noBoost;
    noBoost.bandCount = 16;
    noBoost.profile = MasteringProfile::Fullness;
    noBoost.maxBoostDb = 0.0;
    const auto noBoostAnalysis = analyzeMastering(timeline, noBoost);
    for (const auto& stem : noBoostAnalysis.stems) {
        for (const auto& band : stem.bands)
            CHECK(band.suggestedGainDb <= 0.0001);
    }
}

VWTEST("mastering analysis detects phase cancellation and stem offsets") {
    Timeline aligned;
    std::string error;
    REQUIRE(aligned.addAudioTrack("a", tone(200.0, 0.3, 0.3), 0, &error));
    REQUIRE(aligned.addAudioTrack("b", tone(200.0, 0.3, 0.3), 0, &error));
    MasteringOptions options;
    options.bandCount = 12;
    options.fftSize = 1024;
    options.hopSize = 512;
    const auto inPhase = analyzeMastering(aligned, options);
    REQUIRE(inPhase.valid);
    CHECK(inPhase.phaseCoherence > 0.95);

    Timeline inverted;
    REQUIRE(inverted.addAudioTrack("a", tone(200.0, 0.3, 0.3), 0, &error));
    REQUIRE(inverted.addAudioTrack(
        "b", tone(200.0, 0.3, 0.3, 48000, 1, kPi), 0, &error));
    const auto outOfPhase = analyzeMastering(inverted, options);
    REQUIRE(outOfPhase.valid);
    CHECK(outOfPhase.phaseCoherence < 0.2);

    Timeline offset;
    REQUIRE(offset.addAudioTrack("a", tone(200.0, 0.2, 0.3), 0, &error));
    REQUIRE(offset.addAudioTrack("b", tone(200.0, 0.2, 0.3), 2400, &error));
    const auto shifted = analyzeMastering(offset, options);
    REQUIRE(shifted.valid);
    CHECK_EQ(shifted.frames, int64_t{12000});
    CHECK_EQ(shifted.stems.size(), size_t{2});
}

VWTEST("mastering stereo power survives out-of-phase channels") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack(
        "wide", tone(200.0, 0.3, 0.3, 48000, 2), 0, &error));
    MasteringOptions options;
    options.bandCount = 12;
    options.fftSize = 1024;
    options.hopSize = 512;
    const auto analysis = analyzeMastering(timeline, options, &error);
    REQUIRE(analysis.valid);
    const int band = bandFor(analysis, 200.0);
    REQUIRE(band >= 0);
    CHECK(analysis.stems[0].bands[static_cast<size_t>(band)].powerDb > -40.0);
    CHECK_NEAR(analysis.stems[0].correlation, -1.0, 0.01);
}

VWTEST("mastering analysis handles muted stems and empty timelines") {
    Timeline timeline = makeTimeline();
    REQUIRE(timeline.setTrackMuted(1, true));
    MasteringOptions options;
    options.bandCount = 8;
    const auto visible = analyzeMastering(timeline, options);
    REQUIRE(visible.valid);
    CHECK_EQ(visible.stems.size(), size_t{2});
    options.includeMuted = true;
    const auto all = analyzeMastering(timeline, options);
    REQUIRE(all.valid);
    CHECK_EQ(all.stems.size(), size_t{3});
    CHECK(all.stems[1].muted);

    Timeline empty;
    std::string error;
    const auto none = analyzeMastering(empty, {}, &error);
    CHECK(!none.valid);
    CHECK(!error.empty());
}

VWTEST("mastering report serializes and saves") {
    const Timeline timeline = makeTimeline();
    MasteringOptions options;
    options.bandCount = 8;
    options.profile = MasteringProfile::Fullness;
    const auto analysis = analyzeMastering(timeline, options);
    REQUIRE(analysis.valid);
    const nlohmann::json report = masteringAnalysisToJson(analysis);
    CHECK(report["ok"].get<bool>());
    CHECK_EQ(report["profile"], "fullness");
    CHECK_EQ(report["stems"].size(), size_t{3});
    CHECK_EQ(report["bands"].size(), size_t{8});
    CHECK(report["mix"].contains("true_peak_db"));

    TempDirectory directory;
    const std::string path =
        (directory.path() / "mastering.json").string();
    std::string error;
    REQUIRE(saveMasteringAnalysis(analysis, path, &error));
    CHECK(error.empty());
    std::ifstream in(path);
    REQUIRE(static_cast<bool>(in));
    nlohmann::json loaded;
    in >> loaded;
    CHECK_EQ(loaded["band_count"], 8);
    CHECK_EQ(loaded["stems"].size(), size_t{3});
}

VWTEST("mastering profile names round-trip") {
    for (MasteringProfile profile :
         {MasteringProfile::Balanced, MasteringProfile::Definition,
          MasteringProfile::Separation, MasteringProfile::Fullness,
          MasteringProfile::Loudness, MasteringProfile::Custom}) {
        MasteringProfile parsed = MasteringProfile::Balanced;
        CHECK(masteringProfileFromName(masteringProfileName(profile), parsed));
        CHECK(parsed == profile);
    }
    MasteringProfile invalid = MasteringProfile::Balanced;
    CHECK(!masteringProfileFromName("nope", invalid));
}

VWTEST("mastering processing renders constrained processed stems") {
    const Timeline timeline = makeTimeline();
    MasteringOptions analysisOptions;
    analysisOptions.fftSize = 1024;
    analysisOptions.hopSize = 512;
    analysisOptions.bandCount = 16;
    analysisOptions.profile = MasteringProfile::Definition;
    std::string error;
    const MasteringAnalysis analysis =
        analyzeMastering(timeline, analysisOptions, &error);
    REQUIRE(analysis.valid);

    MasteringProcessingOptions renderOptions;
    renderOptions.fftSize = 1024;
    renderOptions.hopSize = 256;
    renderOptions.limitMix = true;
    renderOptions.ceilingDb = -1.0;
    const MasteringRenderResult rendered =
        renderMastering(timeline, analysis, renderOptions, &error);
    REQUIRE(rendered.valid);
    CHECK(error.empty());
    CHECK_EQ(rendered.stems.size(), size_t{3});
    CHECK(!rendered.mix.empty());
    CHECK(rendered.mix.numChannels() == 2);
    CHECK_EQ(rendered.mix.numFrames(), timeline.lengthFrames());
    CHECK(rendered.mixTruePeakDb <= -0.95);
    for (const auto& stem : rendered.stems) {
        CHECK(!stem.audio.empty());
        CHECK_EQ(stem.audio.sampleRate(), timeline.sampleRate());
        CHECK_EQ(stem.audio.numFrames(),
                 timeline.track(stem.trackIndex).document.numFrames());
        for (int64_t i = 0; i < stem.audio.numFrames(); ++i)
            CHECK(std::isfinite(stem.audio.sample(0, i)));
    }

    const MasteringRenderResult again =
        renderMastering(timeline, analysis, renderOptions, &error);
    REQUIRE(again.valid);
    REQUIRE(again.stems.size() == rendered.stems.size());
    for (size_t i = 0; i < rendered.stems.size(); ++i) {
        CHECK(again.stems[i].audio.equals(rendered.stems[i].audio));
    }

    MasteringAnalysis empty;
    CHECK(!renderMastering(timeline, empty, {}, &error).valid);
    CHECK(!error.empty());
}
