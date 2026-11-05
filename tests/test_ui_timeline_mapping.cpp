#include "test_framework.h"

#include "ui/editor/TimelineMapping.h"

using namespace vwave;
using namespace vwave::ui;

VWTEST("timeline mapping converts between frames and pixels") {
    WaveformViewport view;
    view.startFrame = 1000.0;
    view.visibleFrames = 2000.0;
    const Rect lane{100, 20, 400, 80};

    CHECK_EQ(timelineXForFrame(view, lane, 2000), 300);
    CHECK_EQ(timelineFrameAtX(view, lane, 300, 4000), 2000);
    CHECK_EQ(timelineFrameAtX(view, lane, 50, 4000), 750);
    CHECK_EQ(timelineFrameAtX(view, lane, 600, 4000), 3500);
    CHECK_EQ(timelineLooseFrameAtX(view, lane, 600), 3500);
}
