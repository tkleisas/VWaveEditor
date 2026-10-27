#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Processing.h"
#include "vwave/Timeline.h"

#include <cmath>
#include <filesystem>
#include <vector>

using namespace vwave;

namespace {

Audio constantAudio(int64_t frames, float value, int sampleRate = 48000,
                    int channels = 1) {
    Audio audio(channels, frames, sampleRate);
    for (int ch = 0; ch < channels; ++ch) {
        for (int64_t i = 0; i < frames; ++i)
            audio.sample(ch, i) = value;
    }
    return audio;
}

Audio sineAudio(double frequency, int64_t frames, int sampleRate = 48000) {
    Audio audio(1, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(
            0.5 * std::sin(2.0 * 3.14159265358979323846 * frequency *
                           static_cast<double>(i) / sampleRate));
    }
    return audio;
}

} // namespace

VWTEST("timeline adds tracks and reports project length") {
    Timeline timeline;
    CHECK(timeline.empty());
    CHECK_EQ(timeline.trackCount(), 0);
    CHECK_EQ(timeline.lengthFrames(), int64_t{0});
    CHECK_EQ(timeline.activeTrack(), -1);
    std::string error;
    REQUIRE(timeline.addAudioTrack("one", constantAudio(1000, 0.2f), 0,
                                   &error));
    CHECK(error.empty());
    CHECK_EQ(timeline.sampleRate(), 48000);
    CHECK_EQ(timeline.trackCount(), 1);
    CHECK_EQ(timeline.activeTrack(), 0);
    CHECK_EQ(timeline.lengthFrames(), int64_t{1000});

    REQUIRE(timeline.addAudioTrack("two", constantAudio(2000, 0.1f), 500,
                                   &error));
    CHECK_EQ(timeline.trackCount(), 2);
    CHECK_EQ(timeline.lengthFrames(), int64_t{2500});
    CHECK_NEAR(timeline.duration(), 2500.0 / 48000.0, 1e-9);
    CHECK_EQ(timeline.track(1).startFrame, int64_t{500});
}

VWTEST("timeline mixdown respects offsets and gain") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", constantAudio(1000, 0.2f)));
    REQUIRE(timeline.addAudioTrack("two", constantAudio(2000, 0.1f), 500));
    const Audio mix = timeline.mixdown();
    CHECK_EQ(mix.numChannels(), 2);
    CHECK_EQ(mix.sampleRate(), 48000);
    CHECK_EQ(mix.numFrames(), int64_t{2500});
    CHECK_NEAR(mix.sample(0, 0), 0.2 * 0.70710678, 1e-5);
    CHECK_NEAR(mix.sample(1, 0), 0.2 * 0.70710678, 1e-5);
    CHECK_NEAR(mix.sample(0, 600), 0.3 * 0.70710678, 1e-5);
    REQUIRE(timeline.setTrackGainDb(0, -6.0));
    const Audio quieter = timeline.mixdown();
    CHECK_NEAR(quieter.sample(0, 600),
               (0.2 * 0.501187 + 0.1) * 0.70710678, 1e-4);
}

VWTEST("timeline mixdown respects mute solo and arm") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("a", constantAudio(1000, 0.2f)));
    REQUIRE(timeline.addAudioTrack("b", constantAudio(1000, 0.1f)));
    REQUIRE(timeline.setTrackMuted(1, true));
    Audio mix = timeline.mixdown();
    CHECK_NEAR(mix.sample(0, 500), 0.2 * 0.70710678, 1e-5);
    REQUIRE(timeline.setTrackMuted(1, false));
    REQUIRE(timeline.setTrackSolo(1, true));
    mix = timeline.mixdown();
    CHECK_NEAR(mix.sample(0, 500), 0.1 * 0.70710678, 1e-5);
    CHECK(timeline.anySolo());
    REQUIRE(timeline.setTrackSolo(1, false));
    REQUIRE(timeline.setTrackArmed(0, true));
    CHECK_EQ(timeline.armedTrack(), 0);
    REQUIRE(timeline.setTrackArmed(0, false));
    CHECK_EQ(timeline.armedTrack(), -1);
}

VWTEST("timeline pan writes independent stereo channels") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("mono", constantAudio(100, 1.0f)));
    REQUIRE(timeline.setTrackPan(0, -1.0));
    Audio mix = timeline.mixdown();
    CHECK_NEAR(mix.sample(0, 10), 1.0, 1e-5);
    CHECK_NEAR(mix.sample(1, 10), 0.0, 1e-5);
    REQUIRE(timeline.setTrackPan(0, 1.0));
    mix = timeline.mixdown();
    CHECK_NEAR(mix.sample(0, 10), 0.0, 1e-5);
    CHECK_NEAR(mix.sample(1, 10), 1.0, 1e-5);
    REQUIRE(timeline.setTrackPan(0, 0.0));
    mix = timeline.mixdown();
    CHECK_NEAR(mix.sample(0, 10), 0.70710678, 1e-5);
    CHECK_NEAR(mix.sample(1, 10), 0.70710678, 1e-5);
}

VWTEST("timeline resamples mismatched track audio") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("base", constantAudio(1000, 0.1f, 48000)));
    std::string error;
    REQUIRE(timeline.addAudioTrack("resampled",
                                   constantAudio(1000, 0.1f, 24000), 0,
                                   &error));
    CHECK(error.empty());
    CHECK_EQ(timeline.track(1).document.sampleRate(), 48000);
    CHECK_EQ(timeline.track(1).document.numFrames(), int64_t{2000});
}

VWTEST("timeline selection clamps and syncs to tracks") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", constantAudio(1000, 0.1f)));
    REQUIRE(timeline.addAudioTrack("two", constantAudio(500, 0.2f), 100));
    timeline.setSelection(-100, 900);
    CHECK(timeline.selection().active);
    CHECK_EQ(timeline.selection().start, int64_t{0});
    CHECK_EQ(timeline.selection().end, int64_t{900});
    CHECK_EQ(timeline.track(0).document.selection().start, int64_t{0});
    CHECK_EQ(timeline.track(0).document.selection().end, int64_t{900});
    CHECK_EQ(timeline.track(1).document.selection().start, int64_t{0});
    CHECK_EQ(timeline.track(1).document.selection().end, int64_t{500});
    CHECK_EQ(timeline.selectedRange().length(), int64_t{900});
    timeline.setSelection(150, 400);
    CHECK_EQ(timeline.track(0).document.selection().start, int64_t{150});
    CHECK_EQ(timeline.track(0).document.selection().end, int64_t{400});
    CHECK_EQ(timeline.track(1).document.selection().start, int64_t{50});
    CHECK_EQ(timeline.track(1).document.selection().end, int64_t{300});
    timeline.clearSelection();
    CHECK(!timeline.selection().active);
    CHECK(!timeline.track(0).document.selection().active);
}

VWTEST("timeline empty tracks and active track management") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addEmptyTrack("armed", 0, &error));
    CHECK_EQ(timeline.trackCount(), 1);
    CHECK(timeline.track(0).empty);
    CHECK_EQ(timeline.track(0).document.numFrames(), int64_t{0});
    CHECK_EQ(timeline.lengthFrames(), int64_t{0});
    REQUIRE(timeline.setTrackArmed(0, true));
    REQUIRE(timeline.addAudioTrack("audio", constantAudio(100, 0.1f), 0,
                                   &error));
    CHECK_EQ(timeline.activeTrack(), 0);
    timeline.setActiveTrack(1);
    CHECK_EQ(timeline.activeTrack(), 1);
    REQUIRE(timeline.removeTrack(1));
    CHECK_EQ(timeline.trackCount(), 1);
    CHECK_EQ(timeline.activeTrack(), 0);
    REQUIRE(timeline.removeTrack(0));
    CHECK(timeline.empty());
    CHECK_EQ(timeline.activeTrack(), -1);
}

VWTEST("timeline can fill an empty armed recording track") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addEmptyTrack("Rec", 0, &error));
    REQUIRE(timeline.track(0).empty);
    REQUIRE(timeline.setTrackAudio(0, constantAudio(480, 0.25f), 240, &error));
    CHECK(error.empty());
    CHECK(!timeline.track(0).empty);
    CHECK_EQ(timeline.track(0).startFrame, int64_t{240});
    CHECK_EQ(timeline.track(0).document.numFrames(), int64_t{480});
    CHECK_EQ(timeline.lengthFrames(), int64_t{720});
    CHECK_NEAR(timeline.mixdown().sample(0, 300),
               0.25 * 0.70710678, 1e-5);
}

VWTEST("timeline track controls clamp values") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", constantAudio(100, 0.1f)));
    REQUIRE(timeline.setTrackGainDb(0, 100.0));
    CHECK_NEAR(timeline.track(0).gainDb, 24.0, 1e-9);
    REQUIRE(timeline.setTrackGainDb(0, -100.0));
    CHECK_NEAR(timeline.track(0).gainDb, -96.0, 1e-9);
    REQUIRE(timeline.setTrackPan(0, -4.0));
    CHECK_NEAR(timeline.track(0).pan, -1.0, 1e-9);
    REQUIRE(timeline.setTrackPan(0, 4.0));
    CHECK_NEAR(timeline.track(0).pan, 1.0, 1e-9);
    CHECK(!timeline.setTrackGainDb(4, 0.0));
    CHECK(!timeline.setTrackName(4, "bad"));
}

VWTEST("timeline project round-trip preserves tracks and controls") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("drums", constantAudio(500, 0.2f), 0,
                                   &error));
    REQUIRE(timeline.addEmptyTrack("armed", 100, &error));
    REQUIRE(timeline.addAudioTrack("bass", sineAudio(110.0, 900), 200, &error));
    REQUIRE(timeline.setTrackGainDb(0, -3.0));
    REQUIRE(timeline.setTrackPan(0, -0.3));
    REQUIRE(timeline.setTrackMuted(0, true));
    REQUIRE(timeline.setTrackSolo(2, true));
    REQUIRE(timeline.setTrackArmed(1, true));
    timeline.track(0).recipe = {
        {"type", "oscilloscope_render"},
        {"version", 1},
        {"shape", "circle"}};
    timeline.setActiveTrack(2);
    timeline.setSelectionScope(SelectionScope::ActiveTrack);
    timeline.setSelection(100, 800);

    TempDirectory directory;
    const std::string path = (directory.path() / "project.vwdoc").string();
    REQUIRE(timeline.saveProject(path, &error));
    CHECK(error.empty());

    Timeline loaded;
    REQUIRE(loaded.loadProject(path, &error));
    CHECK(error.empty());
    CHECK_EQ(loaded.trackCount(), 3);
    CHECK_EQ(loaded.sampleRate(), 48000);
    CHECK_EQ(loaded.activeTrack(), 2);
    CHECK(loaded.selectionScope() == SelectionScope::ActiveTrack);
    CHECK_EQ(loaded.selection().start, int64_t{100});
    CHECK_EQ(loaded.selection().end, int64_t{800});
    CHECK_EQ(loaded.track(0).name, std::string("drums"));
    CHECK(loaded.track(0).muted);
    CHECK_NEAR(loaded.track(0).gainDb, -3.0, 1e-9);
    CHECK_NEAR(loaded.track(0).pan, -0.3, 1e-9);
    CHECK_EQ(loaded.track(1).name, std::string("armed"));
    CHECK(loaded.track(1).empty);
    CHECK(loaded.track(1).armed);
    CHECK_EQ(loaded.track(2).startFrame, int64_t{200});
    CHECK(loaded.track(2).solo);
    CHECK(loaded.track(2).document.audio().equals(
        timeline.track(2).document.audio()));
    CHECK(loaded.mixdown().numFrames() == timeline.mixdown().numFrames());
}

VWTEST("timeline selection scope limits selection to the active track") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("a", constantAudio(1000, 0.25f), 0,
                                   &error));
    REQUIRE(timeline.addAudioTrack("b", constantAudio(1000, 0.25f), 100,
                                   &error));
    timeline.setSelectionScope(SelectionScope::ActiveTrack);
    timeline.setActiveTrack(1);
    timeline.setSelection(200, 400);
    CHECK(!timeline.track(0).document.selection().active);
    REQUIRE(timeline.track(1).document.selection().active);
    CHECK_EQ(timeline.track(1).document.selection().start, int64_t{100});
    CHECK_EQ(timeline.track(1).document.selection().end, int64_t{300});

    timeline.setActiveTrack(0);
    CHECK(timeline.track(0).document.selection().active);
    CHECK(!timeline.track(1).document.selection().active);
    timeline.setSelectionScope(SelectionScope::AllTracks);
    CHECK(timeline.track(0).document.selection().active);
    CHECK(timeline.track(1).document.selection().active);
}

VWTEST("timeline crops one track and shifts tracks in time") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("a", constantAudio(1000, 0.25f), 100,
                                   &error));
    REQUIRE(timeline.addAudioTrack("b", constantAudio(800, 0.5f), 0,
                                   &error));
    timeline.track(0).recipe = {{"type", "test"}};
    timeline.setSelectionScope(SelectionScope::ActiveTrack);
    timeline.setActiveTrack(0);
    timeline.setSelection(350, 650);
    REQUIRE(timeline.cropTrack(0, {}, &error));
    CHECK(error.empty());
    CHECK_EQ(timeline.track(0).startFrame, int64_t{350});
    CHECK_EQ(timeline.track(0).document.numFrames(), int64_t{300});
    CHECK(timeline.track(0).recipe.empty());
    CHECK_EQ(timeline.track(1).startFrame, int64_t{0});
    CHECK_EQ(timeline.track(1).document.numFrames(), int64_t{800});
    CHECK(!timeline.selection().active);

    REQUIRE(timeline.shiftTrack(1, 250, &error));
    CHECK_EQ(timeline.track(1).startFrame, int64_t{250});
    CHECK(timeline.track(1).document.audio().equals(
        constantAudio(800, 0.5f)));
    CHECK(!timeline.shiftTrack(0, -1000, &error));
    CHECK(!error.empty());
    CHECK(!timeline.cropTrack(9, {}, &error));
}

VWTEST("timeline loads a legacy single-document project as one track") {
    Document document = Document::fromAudio(constantAudio(400, 0.25f));
    document.setSelection(10, 100);
    document.gainDb(-3.0);
    TempDirectory directory;
    const std::string path = (directory.path() / "single.vwdoc").string();
    std::string error;
    REQUIRE(document.saveProject(path, &error));

    Timeline timeline;
    REQUIRE(timeline.loadProject(path, &error));
    CHECK_EQ(timeline.trackCount(), 1);
    CHECK_EQ(timeline.track(0).document.numFrames(), int64_t{400});
    CHECK(timeline.track(0).document.audio().equals(document.audio()));
    CHECK_EQ(timeline.selection().start, int64_t{10});
    CHECK_EQ(timeline.selection().end, int64_t{100});
}

VWTEST("timeline rejects invalid tracks and project files") {
    Timeline timeline;
    std::string error;
    CHECK(!timeline.addAudioTrack("empty", Audio{}, 0, &error));
    CHECK(!error.empty());
    error.clear();
    REQUIRE(timeline.addAudioTrack("one", constantAudio(100, 0.1f)));
    CHECK(!timeline.addTrack("bad", Document{}, 0, &error));
    CHECK(!error.empty());
    Timeline loaded;
    CHECK(!loaded.loadProject("/no/such/project.vwdoc", &error));
    CHECK(!error.empty());
}

VWTEST("timeline empty mixdown and length after edits") {
    Timeline timeline;
    CHECK(timeline.mixdown().empty());
    REQUIRE(timeline.addAudioTrack("one", constantAudio(100, 0.1f)));
    timeline.track(0).document.setSelection(0, 50);
    REQUIRE(timeline.track(0).document.deleteSelection());
    CHECK_EQ(timeline.lengthFrames(), int64_t{50});
    CHECK_EQ(timeline.mixdown().numFrames(), int64_t{50});
    timeline.track(0).document.undo();
    CHECK_EQ(timeline.lengthFrames(), int64_t{100});
}

VWTEST("timeline splits stereo into two mono tracks and preserves the mix") {
    Timeline timeline;
    std::string error;
    Audio stereo(2, 400, 48000);
    for (int64_t i = 0; i < 400; ++i) {
        stereo.sample(0, i) = 0.25f;
        stereo.sample(1, i) = 0.5f;
    }
    REQUIRE(timeline.addAudioTrack("wide", stereo, 100, &error));
    REQUIRE(timeline.setTrackGainDb(0, -6.0));
    const Audio before = timeline.mixdown();

    REQUIRE(timeline.splitStereoTrack(0, true, &error));
    CHECK(error.empty());
    CHECK_EQ(timeline.trackCount(), 3);
    CHECK(timeline.track(0).muted);
    CHECK_EQ(timeline.track(1).name, std::string("wide L"));
    CHECK_EQ(timeline.track(2).name, std::string("wide R"));
    CHECK_EQ(timeline.track(1).document.numChannels(), 1);
    CHECK_EQ(timeline.track(2).document.numChannels(), 1);
    CHECK_NEAR(timeline.track(1).pan, -1.0, 1e-9);
    CHECK_NEAR(timeline.track(2).pan, 1.0, 1e-9);
    CHECK_EQ(timeline.track(1).startFrame, int64_t{100});
    CHECK_EQ(timeline.track(2).startFrame, int64_t{100});
    CHECK_EQ(timeline.activeTrack(), 1);
    const Audio after = timeline.mixdown();
    CHECK_EQ(after.numFrames(), before.numFrames());
    for (int64_t i = 0; i < after.numFrames(); ++i) {
        CHECK_NEAR(after.sample(0, i), before.sample(0, i), 1e-6);
        CHECK_NEAR(after.sample(1, i), before.sample(1, i), 1e-6);
    }

    Timeline replaced;
    REQUIRE(replaced.addAudioTrack("wide", stereo, 0, &error));
    REQUIRE(replaced.splitStereoTrack(0, false, &error));
    CHECK_EQ(replaced.trackCount(), 2);
    CHECK_EQ(replaced.activeTrack(), 0);
    CHECK_EQ(replaced.track(0).name, std::string("wide L"));
    CHECK_EQ(replaced.track(1).name, std::string("wide R"));

    Timeline mono;
    REQUIRE(mono.addAudioTrack("mono", constantAudio(100, 0.1f), 0, &error));
    CHECK(!mono.splitStereoTrack(0, true, &error));
    CHECK(!error.empty());
}

VWTEST("timeline combines mono tracks with selectable padding") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("left", constantAudio(100, 0.2f), 0,
                                   &error));
    REQUIRE(timeline.addAudioTrack("short", constantAudio(50, 0.4f), 0,
                                   &error));
    StereoCombineOptions options;
    options.padding = StereoPadding::Back;
    REQUIRE(timeline.combineMonoTracks(0, 1, options, &error));
    CHECK(error.empty());
    CHECK_EQ(timeline.trackCount(), 3);
    CHECK(timeline.track(0).muted);
    CHECK(timeline.track(1).muted);
    CHECK_EQ(timeline.activeTrack(), 2);
    const auto& combined = timeline.track(2);
    CHECK_EQ(combined.document.numChannels(), 2);
    CHECK_EQ(combined.document.numFrames(), int64_t{100});
    CHECK_NEAR(combined.document.audio().sample(0, 10), 0.2, 1e-6);
    CHECK_NEAR(combined.document.audio().sample(1, 10), 0.4, 1e-6);
    CHECK_NEAR(combined.document.audio().sample(1, 80), 0.0, 1e-6);

    Timeline front;
    REQUIRE(front.addAudioTrack("left", constantAudio(100, 0.2f), 0,
                                &error));
    REQUIRE(front.addAudioTrack("short", constantAudio(50, 0.4f), 0,
                                &error));
    options.padding = StereoPadding::Front;
    REQUIRE(front.combineMonoTracks(0, 1, options, &error));
    CHECK_NEAR(front.track(2).document.audio().sample(1, 10), 0.0, 1e-6);
    CHECK_NEAR(front.track(2).document.audio().sample(1, 60), 0.4, 1e-6);

    Timeline both;
    REQUIRE(both.addAudioTrack("left", constantAudio(100, 0.2f), 0,
                               &error));
    REQUIRE(both.addAudioTrack("short", constantAudio(50, 0.4f), 0,
                               &error));
    options.padding = StereoPadding::Both;
    REQUIRE(both.combineMonoTracks(0, 1, options, &error));
    CHECK_NEAR(both.track(2).document.audio().sample(1, 10), 0.0, 1e-6);
    CHECK_NEAR(both.track(2).document.audio().sample(1, 30), 0.4, 1e-6);
    CHECK_NEAR(both.track(2).document.audio().sample(1, 80), 0.0, 1e-6);
}

VWTEST("timeline combines mono tracks at their timeline offsets") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("late", constantAudio(100, 0.2f), 100,
                                   &error));
    REQUIRE(timeline.addAudioTrack("early", constantAudio(50, 0.4f), 0,
                                   &error));
    StereoCombineOptions options;
    options.padding = StereoPadding::Back;
    options.keepOriginals = false;
    REQUIRE(timeline.combineMonoTracks(0, 1, options, &error));
    CHECK_EQ(timeline.trackCount(), 1);
    CHECK_EQ(timeline.activeTrack(), 0);
    const auto& combined = timeline.track(0);
    CHECK_EQ(combined.startFrame, int64_t{0});
    CHECK_EQ(combined.document.numFrames(), int64_t{200});
    CHECK_NEAR(combined.document.audio().sample(0, 10), 0.0, 1e-6);
    CHECK_NEAR(combined.document.audio().sample(0, 110), 0.2, 1e-6);
    CHECK_NEAR(combined.document.audio().sample(1, 10), 0.4, 1e-6);
}

VWTEST("timeline rejects invalid stereo splits and combines") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("a", constantAudio(100, 0.1f), 0, &error));
    REQUIRE(timeline.addAudioTrack("b", constantAudio(100, 0.1f), 0, &error));
    CHECK(!timeline.splitStereoTrack(0, true, &error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!timeline.combineMonoTracks(0, 0, {}, &error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!timeline.combineMonoTracks(0, 8, {}, &error));
    CHECK(!error.empty());
}

VWTEST("timeline uses clip arrangements for length and mixdown") {
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("clips", constantAudio(1000, 0.2f), 100,
                                   &error));
    auto& document = timeline.track(0).document;
    REQUIRE(document.createClipArrangement());
    REQUIRE(document.splitClipAt(400));
    REQUIRE(document.moveClip(1, 600));
    CHECK_EQ(timeline.lengthFrames(), int64_t{1300});
    const Audio mix = timeline.mixdown();
    CHECK_EQ(mix.numFrames(), int64_t{1300});
    CHECK_NEAR(mix.sample(0, 150), 0.2 * 0.70710678, 1e-5);
    CHECK_NEAR(mix.sample(0, 500), 0.0, 1e-6);
    CHECK_NEAR(mix.sample(0, 750), 0.2 * 0.70710678, 1e-5);

    timeline.track(0).startFrame = 0;
    CHECK_EQ(timeline.lengthFrames(), int64_t{1200});
    CHECK_EQ(timeline.mixdown().numFrames(), int64_t{1200});
}
