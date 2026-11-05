#pragma once

#include "ui/editor/TimelineCursor.h"

#include "vwave/Timeline.h"
#include "vwave/UiModel.h"

#include <vector>

namespace vwave::ui {

struct TimelineCanvasLayout {
    Rect area;
    Rect ruler;
    Rect overview;
    Rect headerColumn;
    Rect laneArea;
    int rowHeight = 1;
    int trackScroll = 0;
    bool clipToolEnabled = false;
};

struct TimelineCanvasRow {
    Rect header;
    Rect lane;
    int trackIndex = -1;
    int startX = 0;
    int endX = 0;
    bool hasAudio = false;
};

struct TimelineCanvasClip {
    Rect rect;
    int trackIndex = -1;
    int clipIndex = -1;
};

enum class TimelineHitKind {
    None,
    Ruler,
    Overview,
    TrackHeader,
    Lane,
    SelectionStart,
    SelectionEnd,
    SelectionRepeat,
    SelectionStretch,
    SelectionMove,
    ClipBody,
    ClipEnd,
    TrackTrimStart,
    TrackTrimEnd,
    TrackStretch,
    WaveArea,
};

struct TimelineHit {
    TimelineHitKind kind = TimelineHitKind::None;
    int trackIndex = -1;
    int clipIndex = -1;
    int64_t frame = 0;
    Rect bounds;
    Rect lane;
};

class TimelineCanvas {
public:
    void layout(const vwave::Timeline& timeline,
                const vwave::WaveformViewport& viewport,
                const TimelineCanvasLayout& layout);

    const std::vector<TimelineCanvasRow>& rows() const { return m_rows; }
    const std::vector<TimelineCanvasClip>& clips() const { return m_clips; }
    const TimelineCanvasRow* rowForTrack(int trackIndex) const;

    int xForFrame(const Rect& lane, int64_t frame) const;
    int64_t frameAtX(const Rect& lane, int x) const;

    Rect selectionRect(const vwave::Selection& selection,
                       vwave::SelectionScope scope, int activeTrack) const;
    CursorShape cursorAt(Point point, const vwave::Selection& selection,
                         vwave::SelectionScope scope, int activeTrack,
                         bool selectionActive) const;
    TimelineHit hitTest(Point point, const vwave::Selection& selection,
                        vwave::SelectionScope scope, int activeTrack,
                        bool selectionActive) const;

private:
    std::vector<TimelineCanvasRow> m_rows;
    std::vector<TimelineCanvasClip> m_clips;
    vwave::WaveformViewport m_viewport;
    int64_t m_totalFrames = 0;
    Rect m_area;
    Rect m_ruler;
    Rect m_overview;
    Rect m_laneArea;
    bool m_clipToolEnabled = false;
};

} // namespace vwave::ui
