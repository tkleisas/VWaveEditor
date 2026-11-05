#include "ui/editor/TimelineCanvas.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace vwave::ui {

void TimelineCanvas::layout(const vwave::Timeline& timeline,
                            const vwave::WaveformViewport& viewport,
                            const TimelineCanvasLayout& layout) {
    m_rows.clear();
    m_clips.clear();
    m_viewport = viewport;
    m_totalFrames = timeline.lengthFrames();
    m_area = layout.area;
    m_ruler = layout.ruler;
    m_overview = layout.overview;
    m_laneArea = layout.laneArea;
    m_clipToolEnabled = layout.clipToolEnabled;

    const int rowHeight = std::max(1, layout.rowHeight);
    for (int index = 0; index < timeline.trackCount(); ++index) {
        const int rowY =
            layout.headerColumn.y + index * rowHeight - layout.trackScroll;
        if (rowY + rowHeight <= layout.headerColumn.y ||
            rowY >= layout.headerColumn.y + layout.headerColumn.h)
            continue;
        const auto& track = timeline.track(index);
        const int64_t frames = track.document.numFrames();
        TimelineCanvasRow row;
        row.header = {layout.headerColumn.x, rowY + 2,
                      layout.headerColumn.w, rowHeight - 6};
        row.lane = {layout.laneArea.x, rowY + 2, layout.laneArea.w,
                    rowHeight - 6};
        row.trackIndex = index;
        row.startX = xForFrame(row.lane, track.startFrame);
        row.endX = xForFrame(row.lane, track.startFrame + frames);
        row.hasAudio = !track.empty && frames > 0;
        m_rows.push_back(row);

        if (!layout.clipToolEnabled || track.document.clips().empty())
            continue;
        const auto& clips = track.document.clips();
        for (size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex) {
            const auto& clip = clips[clipIndex];
            const int64_t clipFrames = vwave::clipOutputFrames(clip);
            const int64_t globalStart =
                track.startFrame + clip.timelineStart;
            const int left = xForFrame(row.lane, globalStart);
            const int right = xForFrame(row.lane, globalStart + clipFrames);
            m_clips.push_back(
                {{std::min(left, right), row.lane.y, std::abs(right - left),
                  row.lane.h},
                 index, static_cast<int>(clipIndex)});
        }
    }
}

int TimelineCanvas::xForFrame(const Rect& lane, int64_t frame) const {
    return lane.x + static_cast<int>(std::lround(
                        m_viewport.pixelAt(frame, std::max(1, lane.w))));
}

int64_t TimelineCanvas::frameAtX(const Rect& lane, int x) const {
    return m_viewport.frameAt(x - lane.x, std::max(1, lane.w),
                              m_totalFrames);
}

Rect TimelineCanvas::selectionRect(const vwave::Selection& selection,
                                   vwave::SelectionScope scope,
                                   int activeTrack) const {
    if (!selection.active || m_area.w <= 0) return {};
    Rect bounds = m_area;
    if (scope == vwave::SelectionScope::ActiveTrack &&
        activeTrack >= 0) {
        for (const auto& row : m_rows) {
            if (row.trackIndex == activeTrack) {
                bounds = row.lane;
                break;
            }
        }
    }
    const int left = std::clamp(xForFrame(bounds, selection.start),
                                bounds.x, bounds.x + bounds.w);
    const int right = std::clamp(xForFrame(bounds, selection.end),
                                 bounds.x, bounds.x + bounds.w);
    return {left, bounds.y, std::max(1, right - left), bounds.h};
}

CursorShape TimelineCanvas::cursorAt(
    Point point, const vwave::Selection& selection,
    vwave::SelectionScope scope, int activeTrack,
    bool selectionActive) const {
    TimelineCursorGeometry geometry;
    geometry.waveArea = m_area;
    geometry.ruler = m_ruler;
    geometry.overview = m_overview;
    geometry.clipToolEnabled = m_clipToolEnabled;
    geometry.tracks.reserve(m_rows.size());
    for (const auto& row : m_rows) {
        geometry.tracks.push_back(
            {row.header, row.lane, row.trackIndex, row.startX, row.endX,
             row.hasAudio});
    }
    geometry.clips.reserve(m_clips.size());
    for (const auto& clip : m_clips)
        geometry.clips.push_back(
            {clip.rect, clip.trackIndex, clip.clipIndex});
    if (selectionActive) {
        geometry.selection = selectionRect(selection, scope, activeTrack);
        geometry.selectionActive =
            geometry.selection.w > 0 && geometry.selection.h > 0;
    }
    return timelineCursorAt(geometry, point);
}

} // namespace vwave::ui
