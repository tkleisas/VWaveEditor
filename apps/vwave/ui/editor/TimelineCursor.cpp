#include "ui/editor/TimelineCursor.h"

#include <algorithm>
#include <cstdlib>

namespace vwave::ui {

namespace {

bool containsAny(const Rect& rect, Point point) {
    return rect.w > 0 && rect.h > 0 && rect.contains(point);
}

const TimelineClipVisual* clipAt(const TimelineCursorGeometry& geometry,
                                 Point point) {
    for (auto it = geometry.clips.rbegin(); it != geometry.clips.rend(); ++it) {
        if (containsAny(it->rect, point)) return &*it;
    }
    return nullptr;
}

} // namespace

CursorShape timelineCursorAt(const TimelineCursorGeometry& geometry,
                             Point point) {
    if (containsAny(geometry.overview, point)) return CursorShape::Hand;
    if (containsAny(geometry.ruler, point)) return CursorShape::SizeWE;

    for (const auto& track : geometry.tracks) {
        if (containsAny(track.header, point)) return CursorShape::SizeAll;
    }

    if (geometry.clipToolEnabled) {
        if (const TimelineClipVisual* clip = clipAt(geometry, point)) {
            constexpr int kEdge = 8;
            if (std::abs(point.x - clip->rect.x) <= kEdge ||
                std::abs(point.x - (clip->rect.x + clip->rect.w)) <= kEdge)
                return CursorShape::SizeWE;
            return CursorShape::SizeAll;
        }
    }

    if (geometry.selectionActive) {
        const Rect& selection = geometry.selection;
        if (selection.w > 0 && selection.h > 0) {
            const Rect startHandle{selection.x - 4, selection.y, 9,
                                   selection.h};
            const Rect endHandle{selection.x + selection.w - 5, selection.y, 9,
                                 selection.h};
            const Rect repeatHandle{selection.x + selection.w - 6,
                                     selection.y + selection.h - 12, 12, 12};
            if (containsAny(repeatHandle, point)) return CursorShape::SizeAll;
            if (containsAny(startHandle, point) ||
                containsAny(endHandle, point))
                return CursorShape::SizeWE;
        }
    }

    for (const auto& track : geometry.tracks) {
        if (!track.hasAudio || !containsAny(track.lane, point)) continue;
        const Rect& lane = track.lane;
        const Rect stretchHandle{track.endX - 13, lane.y + 2, 11, 11};
        const Rect trimStart{track.startX - 4, lane.y + 2, 8, 14};
        const Rect trimEnd{track.endX - 4, lane.y + lane.h - 16, 8, 14};
        if (containsAny(stretchHandle, point) ||
            containsAny(trimStart, point) || containsAny(trimEnd, point))
            return CursorShape::SizeWE;
    }

    for (const auto& track : geometry.tracks) {
        if (containsAny(track.lane, point)) return CursorShape::Crosshair;
    }
    return CursorShape::Arrow;
}

} // namespace vwave::ui
