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
    timeline.setActiveTrack(2);
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
