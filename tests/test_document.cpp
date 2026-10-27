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

VWTEST("document move copy and repeat are undoable range edits") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 200);

    REQUIRE(doc.moveSelectionTo(400, false));
    CHECK_EQ(doc.numFrames(), int64_t{1000});
    CHECK_EQ(doc.selection().start, int64_t{300});
    CHECK_EQ(doc.selection().end, int64_t{400});
    for (int64_t i = 0; i < 100; ++i) {
        CHECK_NEAR(doc.audio().sample(0, 300 + i),
                   original.sample(0, 100 + i), 1e-9);
        CHECK_NEAR(doc.audio().sample(0, 100 + i),
                   original.sample(0, 200 + i), 1e-9);
    }
    CHECK_EQ(doc.undoCount(), size_t{1});
    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));

    doc.setSelection(100, 200);
    REQUIRE(doc.moveSelectionTo(300, true));
    CHECK_EQ(doc.numFrames(), int64_t{1100});
    CHECK_EQ(doc.selection().start, int64_t{300});
    CHECK_EQ(doc.selection().end, int64_t{400});
    for (int64_t i = 0; i < 100; ++i) {
        CHECK_NEAR(doc.audio().sample(0, 300 + i),
                   original.sample(0, 100 + i), 1e-9);
        CHECK_NEAR(doc.audio().sample(0, 100 + i),
                   original.sample(0, 100 + i), 1e-9);
        CHECK_NEAR(doc.audio().sample(0, 400 + i),
                   original.sample(0, 300 + i), 1e-9);
    }
    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));

    doc.setSelection(100, 200);
    REQUIRE(doc.repeatSelection(2));
    CHECK_EQ(doc.numFrames(), int64_t{1200});
    CHECK_EQ(doc.selection().start, int64_t{100});
    CHECK_EQ(doc.selection().end, int64_t{400});
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int64_t i = 0; i < 100; ++i) {
            CHECK_NEAR(doc.audio().sample(0, 100 + repeat * 100 + i),
                       original.sample(0, 100 + i), 1e-9);
        }
    }
    for (int64_t i = 0; i < 800; ++i) {
        CHECK_NEAR(doc.audio().sample(0, 400 + i),
                   original.sample(0, 200 + i), 1e-9);
    }
    REQUIRE(doc.undo());
    CHECK(doc.audio().equals(original));
}

VWTEST("document time stretch track ignores selection and is undoable") {
    Audio original(2, 1000, 48000);
    for (int64_t i = 0; i < 1000; ++i) {
        const float value = static_cast<float>(
            0.3 * std::sin(2.0 * 3.14159265358979323846 * 440.0 *
                           static_cast<double>(i) / 48000.0));
        original.sample(0, i) = value;
        original.sample(1, i) = value;
    }
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 200);
    REQUIRE(doc.timeStretchTrack(2.0, StretchAlgorithm::Wsola));
    CHECK_EQ(doc.numFrames(), int64_t{2000});
    CHECK_EQ(doc.selection().start, int64_t{0});
    CHECK_EQ(doc.selection().end, int64_t{2000});
    REQUIRE(doc.undo());
    CHECK_EQ(doc.numFrames(), int64_t{1000});
    CHECK(doc.audio().equals(original));
    CHECK_EQ(doc.selection().start, int64_t{100});
    CHECK_EQ(doc.selection().end, int64_t{200});
}

VWTEST("document rejects moves into the source range") {
    Document doc = Document::fromAudio(makeRamp(100));
    doc.setSelection(20, 80);
    std::string error;
    CHECK(!doc.moveSelectionTo(50, false, &error));
    CHECK(!error.empty());
    CHECK_EQ(doc.numFrames(), int64_t{100});
    CHECK_EQ(doc.undoCount(), size_t{0});
}

VWTEST("document range operations survive project round-trip") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    doc.setSelection(100, 200);
    REQUIRE(doc.moveSelectionTo(400, false));
    doc.setSelection(300, 400);
    REQUIRE(doc.repeatSelection(1));
    const Audio expected = doc.audio();
    CHECK_EQ(doc.numFrames(), int64_t{1100});

    TempPath file(".vwdoc");
    std::string error;
    REQUIRE(doc.saveProject(file.string(), &error));
    Document loaded;
    REQUIRE(loaded.loadProject(file.string(), &error));
    CHECK_EQ(loaded.numFrames(), int64_t{1100});
    CHECK(loaded.audio().equals(expected));
    CHECK_EQ(loaded.undoLabel(), std::string("Repeat range"));
    REQUIRE(loaded.undo());
    CHECK_EQ(loaded.undoLabel(), std::string("Move range"));
    REQUIRE(loaded.undo());
    CHECK(loaded.audio().equals(original));
}

VWTEST("document clip arrangement splits moves and repeats undoably") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    REQUIRE(doc.createClipArrangement());
    CHECK(doc.hasClips());
    CHECK_EQ(doc.clips().size(), size_t{1});
    CHECK_EQ(doc.undoLabel(), std::string("Create clips"));
    CHECK(doc.audio().equals(original));
    REQUIRE(doc.undo());
    CHECK(!doc.hasClips());
    CHECK(doc.audio().equals(original));
    REQUIRE(doc.redo());
    CHECK_EQ(doc.clips().size(), size_t{1});

    REQUIRE(doc.splitClipAt(400));
    CHECK_EQ(doc.clips().size(), size_t{2});
    CHECK_EQ(doc.undoLabel(), std::string("Split clip"));
    CHECK(doc.audio().equals(original));
    CHECK_EQ(doc.clips()[0].sourceStart, int64_t{0});
    CHECK_EQ(doc.clips()[0].sourceLength, int64_t{400});
    CHECK_EQ(doc.clips()[1].sourceStart, int64_t{400});
    CHECK_EQ(doc.clips()[1].sourceLength, int64_t{600});
    CHECK_EQ(doc.clips()[1].timelineStart, int64_t{400});

    REQUIRE(doc.deleteClip(1));
    CHECK_EQ(doc.clips().size(), size_t{1});
    CHECK_EQ(doc.numFrames(), int64_t{400});
    for (int64_t i = 0; i < 400; ++i)
        CHECK_NEAR(doc.audio().sample(0, i), original.sample(0, i), 1e-9);
    REQUIRE(doc.undo());
    CHECK_EQ(doc.clips().size(), size_t{2});

    REQUIRE(doc.moveClip(1, 600));
    CHECK_EQ(doc.clips()[1].timelineStart, int64_t{600});
    CHECK_EQ(doc.numFrames(), int64_t{1200});
    for (int64_t i = 400; i < 600; ++i)
        CHECK_NEAR(doc.audio().sample(0, i), 0.0f, 1e-9);
    for (int64_t i = 0; i < 600; ++i)
        CHECK_NEAR(doc.audio().sample(0, 600 + i),
                   original.sample(0, 400 + i), 1e-9);

    REQUIRE(doc.repeatClip(1, 1));
    CHECK_EQ(doc.clips().size(), size_t{3});
    CHECK_EQ(doc.numFrames(), int64_t{1800});
    CHECK_EQ(doc.clips()[2].timelineStart, int64_t{1200});
    for (int64_t i = 0; i < 600; ++i)
        CHECK_NEAR(doc.audio().sample(0, 1200 + i),
                   original.sample(0, 400 + i), 1e-9);
    REQUIRE(doc.undo());
    CHECK_EQ(doc.numFrames(), int64_t{1200});
    REQUIRE(doc.undo());
    CHECK_EQ(doc.numFrames(), int64_t{1000});
    CHECK_EQ(doc.clips()[1].timelineStart, int64_t{400});
}

VWTEST("document clip stretch gain and flatten are undoable") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    REQUIRE(doc.createClipArrangement());
    REQUIRE(doc.splitClipAt(400));
    REQUIRE(doc.stretchClip(0, 2.0));
    CHECK_EQ(doc.clips()[0].stretch, 2.0);
    CHECK_EQ(clipOutputFrames(doc.clips()[0]), int64_t{800});
    REQUIRE(doc.undo());

    REQUIRE(doc.setClipGain(0, -6.0));
    CHECK_NEAR(doc.audio().sample(0, 100),
               original.sample(0, 100) * 0.501187, 1e-5);
    REQUIRE(doc.setClipFades(0, 0.001, 0.001));
    CHECK_EQ(doc.clips()[0].fadeInSeconds, 0.001);
    CHECK_NEAR(doc.audio().sample(0, 0), 0.0f, 1e-9);

    const Audio arranged = doc.audio();
    REQUIRE(doc.flattenClips());
    CHECK(!doc.hasClips());
    CHECK(doc.audio().equals(arranged));
    CHECK_EQ(doc.undoLabel(), std::string("Flatten clips"));
    REQUIRE(doc.undo());
    CHECK(doc.hasClips());
    CHECK_EQ(doc.clips().size(), size_t{2});
}

VWTEST("document requires flattening before destructive clip edits") {
    Document doc = Document::fromAudio(makeRamp(1000));
    REQUIRE(doc.createClipArrangement());
    doc.setSelection(100, 200);
    std::string error;
    CHECK(!doc.gainDb(-6.0, &error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!doc.deleteSelection(&error));
    CHECK(!error.empty());

    REQUIRE(doc.flattenClips());
    CHECK(!doc.hasClips());
    CHECK(doc.gainDb(-6.0, &error));
    CHECK(doc.undoLabel() == std::string("Gain -6 dB"));
    REQUIRE(doc.undo());
    CHECK(doc.undoLabel() == std::string("Flatten clips"));
    REQUIRE(doc.undo());
    CHECK(doc.hasClips());
    REQUIRE(doc.redo());
    CHECK(!doc.hasClips());
    REQUIRE(doc.redo());
    CHECK(doc.undoLabel() == std::string("Gain -6 dB"));
    CHECK(!doc.hasClips());
}

VWTEST("document clip project round-trip preserves arrangement") {
    const Audio original = makeRamp(1000);
    Document doc = Document::fromAudio(original);
    REQUIRE(doc.createClipArrangement());
    REQUIRE(doc.splitClipAt(400));
    REQUIRE(doc.moveClip(1, 600));
    REQUIRE(doc.setClipGain(1, -3.0));
    REQUIRE(doc.setClipFades(0, 0.002, 0.003));
    const Audio expected = doc.audio();
    CHECK_EQ(doc.clips().size(), size_t{2});

    TempPath file(".vwdoc");
    std::string error;
    REQUIRE(doc.saveProject(file.string(), &error));
    Document loaded;
    REQUIRE(loaded.loadProject(file.string(), &error));
    CHECK(loaded.hasClips());
    CHECK_EQ(loaded.clips().size(), size_t{2});
    CHECK_EQ(loaded.clips()[1].timelineStart, int64_t{600});
    CHECK_NEAR(loaded.clips()[1].gainDb, -3.0, 1e-9);
    CHECK(loaded.audio().equals(expected));
    CHECK_EQ(loaded.undoLabel(), std::string("Set clip fades"));
    REQUIRE(loaded.undo());
    CHECK_EQ(loaded.numFrames(), int64_t{1200});
    REQUIRE(loaded.redo());
    CHECK(loaded.audio().equals(expected));
}

VWTEST("document clip arrangement preserves stereo channels") {
    Audio original(2, 500, 48000);
    for (int64_t i = 0; i < original.numFrames(); ++i) {
        original.sample(0, i) = 0.25f;
        original.sample(1, i) = -0.5f;
    }
    Document doc = Document::fromAudio(original);
    REQUIRE(doc.createClipArrangement());
    REQUIRE(doc.splitClipAt(200));
    REQUIRE(doc.moveClip(1, 300));
    CHECK_EQ(doc.numFrames(), int64_t{600});
    CHECK_NEAR(doc.audio().sample(0, 100), 0.25, 1e-6);
    CHECK_NEAR(doc.audio().sample(1, 100), -0.5, 1e-6);
    CHECK_NEAR(doc.audio().sample(0, 400), 0.25, 1e-6);
    CHECK_NEAR(doc.audio().sample(1, 400), -0.5, 1e-6);
}

VWTEST("document rejects invalid clip arrangements") {
    Document doc = Document::fromAudio(makeRamp(100));
    std::string error;
    Clip clip;
    clip.sourceStart = 90;
    clip.sourceLength = 20;
    CHECK(!doc.setClipArrangement({clip}, "Bad", &error));
    CHECK(!error.empty());
    CHECK(!doc.hasClips());
    CHECK_EQ(doc.undoCount(), size_t{0});

    error.clear();
    Clip valid;
    valid.sourceLength = 100;
    REQUIRE(doc.setClipArrangement({valid}, "Good", &error));
    CHECK_EQ(doc.undoLabel(), std::string("Good"));
    error.clear();
    CHECK(!doc.deleteClip(0, &error));
    CHECK(!error.empty());
}

VWTEST("document revision changes with audio edits") {
    Document doc = Document::fromAudio(makeRamp(100));
    const uint64_t initial = doc.revision();
    REQUIRE(doc.gainDb(-6.0));
    CHECK(doc.revision() > initial);
    const uint64_t edited = doc.revision();
    REQUIRE(doc.undo());
    CHECK(doc.revision() > edited);
    const uint64_t undone = doc.revision();
    REQUIRE(doc.redo());
    CHECK(doc.revision() > undone);
    const uint64_t redone = doc.revision();
    REQUIRE(doc.createClipArrangement());
    CHECK(doc.revision() > redone);
    const uint64_t clips = doc.revision();
    REQUIRE(doc.flattenClips());
    CHECK(doc.revision() > clips);
}
