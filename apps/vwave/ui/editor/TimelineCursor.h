#pragma once

#include "ui/framework/Geometry.h"

#include <vector>

namespace vwave::ui {

struct TimelineTrackVisual {
    Rect header;
    Rect lane;
    int trackIndex = -1;
    int startX = 0;
    int endX = 0;
    bool hasAudio = false;
};

struct TimelineClipVisual {
    Rect rect;
    int trackIndex = -1;
    int clipIndex = -1;
};

struct TimelineCursorGeometry {
    Rect waveArea;
    Rect ruler;
    Rect overview;
    std::vector<TimelineTrackVisual> tracks;
    std::vector<TimelineClipVisual> clips;
    Rect selection;
    bool selectionActive = false;
    bool clipToolEnabled = false;
};

CursorShape timelineCursorAt(const TimelineCursorGeometry& geometry,
                             Point point);

} // namespace vwave::ui
