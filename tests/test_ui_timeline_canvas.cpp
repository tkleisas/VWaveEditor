#include "test_framework.h"

#include "ui/editor/TimelineCanvas.h"

#include "vwave/Timeline.h"

#include <memory>

using namespace vwave;
using namespace vwave::ui;

namespace {

Audio audio(int64_t frames, int sampleRate = 48000) {
    Audio result(1, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i)
        result.sample(0, i) = 0.1f;
    return result;
}

TimelineCanvasLayout layout() {
    TimelineCanvasLayout layout;
    layout.area = {100, 50, 400, 160};
    layout.ruler = {100, 30, 400, 20};
    layout.overview = {100, 10, 400, 20};
    layout.headerColumn = {0, 50, 100, 200};
    layout.laneArea = {100, 50, 400, 160};
    layout.rowHeight = 80;
    layout.trackScroll = 0;
    layout.clipToolEnabled = false;
    return layout;
}

} // namespace

VWTEST("timeline canvas lays out visible rows and frame mapping") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", audio(1000), 0));
    REQUIRE(timeline.addAudioTrack("two", audio(1000), 500));
    WaveformViewport view;
    view.fit(2000);

    TimelineCanvas canvas;
    canvas.layout(timeline, view, layout());
    REQUIRE(canvas.rows().size() == size_t{2});
    CHECK(canvas.rows()[0].header.x == 0);
    CHECK(canvas.rows()[0].header.w == 100);
    CHECK(canvas.rows()[0].lane.x == 100);
    CHECK(canvas.rows()[0].lane.w == 400);
    CHECK(canvas.rows()[0].trackIndex == 0);
    CHECK(canvas.rows()[1].trackIndex == 1);
    CHECK(canvas.rows()[0].startX == 100);
    CHECK(canvas.rows()[1].startX == 200);
    CHECK(canvas.frameAtX(canvas.rows()[0].lane, 300) == 1000);
}

VWTEST("timeline canvas skips rows outside the viewport") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", audio(1000), 0));
    REQUIRE(timeline.addAudioTrack("two", audio(1000), 0));
    WaveformViewport view;
    view.fit(1000);
    auto config = layout();
    config.trackScroll = 80;

    TimelineCanvas canvas;
    canvas.layout(timeline, view, config);
    REQUIRE(canvas.rows().size() == size_t{1});
    CHECK(canvas.rows()[0].trackIndex == 1);
}

VWTEST("timeline canvas builds clip rects and selection") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", audio(1000), 0));
    REQUIRE(timeline.track(0).document.createClipArrangement());
    REQUIRE(timeline.track(0).document.splitClipAt(500));
    WaveformViewport view;
    view.fit(1000);
    auto config = layout();
    config.clipToolEnabled = true;

    TimelineCanvas canvas;
    canvas.layout(timeline, view, config);
    REQUIRE(canvas.clips().size() == size_t{2});
    CHECK(canvas.clips()[0].trackIndex == 0);
    CHECK(canvas.clips()[0].clipIndex == 0);
    CHECK(canvas.clips()[0].rect.x == 100);
    CHECK(canvas.clips()[1].rect.x == 300);

    Selection selection{200, 600, true};
    const Rect selected = canvas.selectionRect(
        selection, SelectionScope::AllTracks, 0);
    CHECK(selected.x == 180);
    CHECK(selected.w == 160);
    CHECK(canvas.cursorAt({150, 55}, selection, SelectionScope::AllTracks, 0,
                          true) == CursorShape::SizeAll);
}

VWTEST("timeline canvas hit testing follows handle precedence") {
    Timeline timeline;
    REQUIRE(timeline.addAudioTrack("one", audio(1000), 0));
    REQUIRE(timeline.track(0).document.createClipArrangement());
    REQUIRE(timeline.track(0).document.splitClipAt(500));
    WaveformViewport view;
    view.fit(1000);
    auto config = layout();
    config.clipToolEnabled = true;

    TimelineCanvas canvas;
    canvas.layout(timeline, view, config);
    Selection selection{200, 600, true};

    CHECK(canvas.hitTest({150, 55}, selection, SelectionScope::AllTracks, 0,
                         true).kind == TimelineHitKind::ClipBody);
    auto plainConfig = config;
    plainConfig.clipToolEnabled = false;
    TimelineCanvas plain;
    plain.layout(timeline, view, plainConfig);
    CHECK(plain.hitTest({150, 55}, selection, SelectionScope::AllTracks, 0,
                        true).kind == TimelineHitKind::Lane);
    CHECK(canvas.hitTest({290, 55}, selection, SelectionScope::AllTracks, 0,
                         true).kind == TimelineHitKind::ClipBody);
    CHECK(canvas.hitTest({499, 55}, selection, SelectionScope::AllTracks, 0,
                         true).kind == TimelineHitKind::ClipEnd);
    CHECK(canvas.hitTest({30, 55}, selection, SelectionScope::AllTracks, 0,
                         true).kind == TimelineHitKind::TrackHeader);
    CHECK(canvas.hitTest({150, 35}, selection, SelectionScope::AllTracks, 0,
                         true).kind == TimelineHitKind::Ruler);
    CHECK(plain.hitTest({150, 55}, selection, SelectionScope::AllTracks, 0,
                         false).kind == TimelineHitKind::Lane);
}
