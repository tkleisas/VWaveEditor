#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Document.h"

#include <cmath>
#include <filesystem>
#include <vector>

using namespace vwave;

namespace {

Audio makeRamp(int64_t frames, int sampleRate = 48000) {
    Audio audio(2, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(i) / 1000.0f;
        audio.sample(1, i) = -static_cast<float>(i) / 2000.0f;
    }
    return audio;
}

} // namespace

VWTEST("document gain is non-destructive and undoable") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);

    CHECK(doc.canUndo() == false);
    REQUIRE(doc.gainDb(-6.0));
    CHECK(doc.undoCount() == 1);
    CHECK(doc.canUndo());
    CHECK(doc.source().equals(original));
    CHECK(!doc.audio().equals(original));
    CHECK_NEAR(doc.audio().sample(0, 500), original.sample(0, 500) * 0.501187, 1e-5);

    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
    CHECK(doc.canRedo());
    CHECK(doc.source().equals(original));

    REQUIRE(doc.redo());
    CHECK_NEAR(doc.audio().sample(0, 500), original.sample(0, 500) * 0.501187, 1e-5);
}

VWTEST("document selection edits only the selected samples") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 200);
    REQUIRE(doc.gainDb(-200.0));

    for (int64_t i = 0; i < 100; ++i) {
        CHECK_NEAR(doc.audio().sample(0, i), original.sample(0, i), 1e-9);
        CHECK_NEAR(doc.audio().sample(0, 100 + i), 0.0, 1e-9);
        CHECK_NEAR(doc.audio().sample(1, 100 + i), 0.0, 1e-9);
        CHECK_NEAR(doc.audio().sample(0, 200 + i), original.sample(0, 200 + i), 1e-9);
    }
}

VWTEST("document delete is non-destructive and changes duration") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 300);

    REQUIRE(doc.deleteSelection());
    CHECK_EQ(doc.numFrames(), int64_t{800});
    CHECK_EQ(doc.source().numFrames(), int64_t{1000});
    CHECK(doc.source().equals(original));
    CHECK_NEAR(doc.audio().sample(0, 100), original.sample(0, 300), 1e-9);
    CHECK(doc.selection().empty());

    REQUIRE(doc.undo());
    CHECK_EQ(doc.numFrames(), int64_t{1000});
    CHECK(doc.audio().equals(original));
    CHECK(doc.selection().active);
    CHECK_EQ(doc.selection().start, int64_t{100});
    CHECK_EQ(doc.selection().end, int64_t{300});
}

VWTEST("document cut copy paste and undo") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 200);
    REQUIRE(doc.copySelection());
    CHECK(doc.hasClipboard());
    CHECK_EQ(doc.clipboard().numFrames(), int64_t{100});

    REQUIRE(doc.pasteAt(300));
    CHECK_EQ(doc.numFrames(), int64_t{1100});
    for (int64_t i = 0; i < 100; ++i)
        CHECK_NEAR(doc.audio().sample(0, 300 + i), original.sample(0, 100 + i), 1e-9);

    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));

    doc.setSelection(100, 200);
    REQUIRE(doc.cutSelection());
    CHECK_EQ(doc.numFrames(), int64_t{900});
    CHECK(doc.hasClipboard());
    CHECK(doc.selection().empty());

    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
}

VWTEST("document paste replaces an active selection") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 200);
    REQUIRE(doc.copySelection());
    doc.setSelection(400, 450);
    REQUIRE(doc.paste());
    CHECK_EQ(doc.numFrames(), int64_t{1050});
    for (int64_t i = 0; i < 100; ++i)
        CHECK_NEAR(doc.audio().sample(0, 400 + i), original.sample(0, 100 + i), 1e-9);
    CHECK_EQ(doc.selection().start, int64_t{400});
    CHECK_EQ(doc.selection().end, int64_t{500});
}

VWTEST("document crop keeps selection and preserves source") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 300);
    REQUIRE(doc.cropToSelection());
    CHECK_EQ(doc.numFrames(), int64_t{200});
    CHECK_NEAR(doc.audio().sample(0, 0), original.sample(0, 100), 1e-9);
    CHECK(doc.source().equals(original));

    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
}

VWTEST("document insert and replace operations") {
    const Audio original = makeRamp(100);
    Document doc = Document::fromAudio(original);

    Audio inserted(2, 10, 48000);
    for (int64_t i = 0; i < 10; ++i) {
        inserted.sample(0, i) = 0.5f;
        inserted.sample(1, i) = -0.5f;
    }

    REQUIRE(doc.insertAudio(40, inserted));
    CHECK_EQ(doc.numFrames(), int64_t{110});
    CHECK_NEAR(doc.audio().sample(0, 40), 0.5, 1e-9);
    CHECK_NEAR(doc.audio().sample(0, 50), original.sample(0, 40), 1e-9);
    CHECK_EQ(doc.selection().start, int64_t{40});
    CHECK_EQ(doc.selection().end, int64_t{50});

    REQUIRE(doc.undo());
    doc.setSelection(0, 10);
    REQUIRE(doc.replaceSelection(inserted));
    CHECK_EQ(doc.numFrames(), int64_t{100});
    CHECK_NEAR(doc.audio().sample(0, 0), 0.5, 1e-9);
    CHECK_EQ(doc.selection().start, int64_t{0});
    CHECK_EQ(doc.selection().end, int64_t{10});

    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
}

VWTEST("document rejects invalid operation ranges") {
    Document doc = Document::fromAudio(makeRamp(100));
    std::string error;
    CHECK(!doc.apply(Operation::gain(-6.0, {50, 200}), &error));
    CHECK(!error.empty());
    CHECK(doc.canUndo() == false);
    CHECK_EQ(doc.numFrames(), int64_t{100});
}

VWTEST("document selection is clamped") {
    Document doc = Document::fromAudio(makeRamp(100));
    doc.setSelection(-50, 500);
    CHECK_EQ(doc.selection().start, int64_t{0});
    CHECK_EQ(doc.selection().end, int64_t{100});
    CHECK_EQ(doc.activeRange().start, int64_t{0});
    CHECK_EQ(doc.activeRange().end, int64_t{100});
    doc.clearSelection();
    CHECK_EQ(doc.activeRange().start, int64_t{0});
    CHECK_EQ(doc.activeRange().end, int64_t{100});
}

VWTEST("document active range falls back to the whole document") {
    Document doc = Document::fromAudio(makeRamp(321));
    CHECK_EQ(doc.activeRange().start, int64_t{0});
    CHECK_EQ(doc.activeRange().end, int64_t{321});
    doc.setSelection(10, 20);
    CHECK_EQ(doc.activeRange().start, int64_t{10});
    CHECK_EQ(doc.activeRange().end, int64_t{20});
    CHECK_EQ(doc.selectedRange().start, int64_t{10});
    CHECK_EQ(doc.selectedRange().end, int64_t{20});
}

VWTEST("document undo redo stack handles branches") {
    Document doc = Document::fromAudio(makeRamp(100));
    doc.setSelection(0, 20);
    REQUIRE(doc.gainDb(-6));
    doc.setSelection(20, 40);
    REQUIRE(doc.gainDb(-6));
    CHECK_EQ(doc.undoCount(), size_t{2});

    REQUIRE(doc.undo());
    CHECK_EQ(doc.undoCount(), size_t{1});
    CHECK_EQ(doc.redoCount(), size_t{1});
    doc.setSelection(40, 60);
    REQUIRE(doc.gainDb(-6));
    CHECK_EQ(doc.undoCount(), size_t{2});
    CHECK_EQ(doc.redoCount(), size_t{0});
}

VWTEST("document markers and slices are project metadata") {
    Document doc = Document::fromAudio(makeRamp(1000));
    REQUIRE(doc.addMarker(0, "start"));
    REQUIRE(doc.addMarker(500, "drop"));
    CHECK_EQ(doc.markers().size(), size_t{2});
    CHECK_EQ(doc.markers()[1].name, std::string("drop"));
    CHECK(doc.removeMarker(0));
    CHECK_EQ(doc.markers().size(), size_t{1});

    doc.setSlices({{0, 100, "one"}, {100, 300, "two"}});
    CHECK_EQ(doc.slices().size(), size_t{2});
    CHECK_EQ(doc.slices()[1].range.start, int64_t{100});
    doc.clearSlices();
    CHECK(doc.slices().empty());
}

VWTEST("document project round-trip preserves source history and metadata") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSourcePath("/tmp/original.wav");
    doc.setSelection(100, 200);
    REQUIRE(doc.gainDb(-3));
    doc.setSelection(200, 300);
    REQUIRE(doc.deleteSelection());
    doc.setSelection(50, 150);
    REQUIRE(doc.addMarker(50, "mark"));
    doc.addMarker(150, "end");
    doc.setSlices({{0, 50, "a"}, {50, 150, "b"}});

    TempPath file(".vwdoc");
    std::string error;
    REQUIRE(doc.saveProject(file.string(), &error));
    CHECK(error.empty());

    Document loaded;
    REQUIRE(loaded.loadProject(file.string(), &error));
    CHECK_EQ(loaded.sourcePath(), std::string("/tmp/original.wav"));
    CHECK_EQ(loaded.undoCount(), size_t{2});
    CHECK_EQ(loaded.selection().start, int64_t{50});
    CHECK_EQ(loaded.selection().end, int64_t{150});
    CHECK_EQ(loaded.markers().size(), size_t{2});
    CHECK_EQ(loaded.slices().size(), size_t{2});
    CHECK(loaded.source().equals(doc.source()));
    CHECK(loaded.audio().equals(doc.audio()));

    REQUIRE(loaded.undo());
    CHECK_EQ(loaded.undoCount(), size_t{1});
    CHECK_EQ(loaded.numFrames(), int64_t{1000});
}

VWTEST("document clear history keeps current audio") {
    Document doc = Document::fromAudio(makeRamp(100));
    REQUIRE(doc.gainDb(-6));
    const Audio rendered = doc.audio();
    doc.clearHistory();
    CHECK(doc.canUndo() == false);
    CHECK(doc.audio().equals(rendered));
    CHECK(doc.source().numFrames() == 100);
}

VWTEST("document rejects saving an empty document") {
    Document doc;
    TempPath file(".vwdoc");
    std::string error;
    CHECK(!doc.saveProject(file.string(), &error));
    CHECK(!error.empty());
}
