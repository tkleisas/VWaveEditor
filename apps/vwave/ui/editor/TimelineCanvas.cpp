#include "ui/editor/TimelineCanvas.h"

#include "ui/editor/TimelineMapping.h"

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

const TimelineCanvasRow* TimelineCanvas::rowForTrack(int trackIndex) const {
    for (const auto& row : m_rows)
        if (row.trackIndex == trackIndex) return &row;
    return nullptr;
}

int TimelineCanvas::xForFrame(const Rect& lane, int64_t frame) const {
    return timelineXForFrame(m_viewport, lane, frame);
}

int64_t TimelineCanvas::frameAtX(const Rect& lane, int x) const {
    return timelineFrameAtX(m_viewport, lane, x, m_totalFrames);
}

Rect TimelineCanvas::selectionRect(const vwave::Selection& selection,
                                   vwave::SelectionScope scope,
                                   int activeTrack) const {
    if (!selection.active || m_laneArea.w <= 0) return {};
    Rect bounds = m_laneArea;
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

TimelineHit TimelineCanvas::hitTest(
    Point point, const vwave::Selection& selection,
    vwave::SelectionScope scope, int activeTrack,
    bool selectionActive) const {
    auto hit = [](TimelineHitKind kind, Rect bounds, int track = -1,
                  int clip = -1, int64_t frame = 0) {
        return TimelineHit{kind, track, clip, frame, bounds, {}};
    };

    if (m_overview.contains(point))
        return hit(TimelineHitKind::Overview, m_overview);
    if (m_ruler.contains(point))
        return hit(TimelineHitKind::Ruler, m_ruler, -1, -1,
                   frameAtX(m_ruler, point.x));

    for (const auto& row : m_rows) {
        if (row.header.contains(point))
            return hit(TimelineHitKind::TrackHeader, row.header,
                       row.trackIndex);
    }

    const TimelineCanvasRow* row = nullptr;
    for (const auto& candidate : m_rows) {
        if (candidate.lane.contains(point)) {
            row = &candidate;
            break;
        }
    }
    if (row) {
        const int64_t frame = frameAtX(row->lane, point.x);
        if (m_clipToolEnabled) {
            for (auto it = m_clips.rbegin(); it != m_clips.rend(); ++it) {
                if (it->trackIndex != row->trackIndex ||
                    !it->rect.contains(point))
                    continue;
                constexpr int kEdge = 8;
                if (std::abs(point.x - (it->rect.x + it->rect.w)) <= kEdge)
                    {
                        TimelineHit result =
                            hit(TimelineHitKind::ClipEnd, it->rect,
                                it->trackIndex, it->clipIndex, frame);
                        result.lane = row->lane;
                        return result;
                    }
                {
                    TimelineHit result =
                        hit(TimelineHitKind::ClipBody, it->rect,
                            it->trackIndex, it->clipIndex, frame);
                    result.lane = row->lane;
                    return result;
                }
            }
        }

        if (selectionActive) {
            const Rect selected = selectionRect(selection, scope, activeTrack);
            if (selected.w > 0 && selected.h > 0) {
                const Rect startHandle{selected.x - 4, selected.y, 9,
                                       selected.h};
                const Rect endHandle{selected.x + selected.w - 5, selected.y,
                                     9, selected.h};
                const Rect repeatHandle{selected.x + selected.w - 6,
                                         selected.y + selected.h - 12, 12, 12};
                const Rect stretchHandle{selected.x + selected.w - 12,
                                          selected.y, 10, 10};
                const Rect moveHandle{selected.x + selected.w / 2 - 7,
                                      selected.y, 14, 8};
                if (repeatHandle.contains(point)) {
                    TimelineHit result =
                        hit(TimelineHitKind::SelectionRepeat, selected,
                            row->trackIndex, -1, frame);
                    result.lane = row->lane;
                    return result;
                }
                if (stretchHandle.contains(point)) {
                    TimelineHit result =
                        hit(TimelineHitKind::SelectionStretch, selected,
                            row->trackIndex, -1, frame);
                    result.lane = row->lane;
                    return result;
                }
                if (moveHandle.contains(point)) {
                    TimelineHit result =
                        hit(TimelineHitKind::SelectionMove, selected,
                            row->trackIndex, -1, frame);
                    result.lane = row->lane;
                    return result;
                }
                if (startHandle.contains(point)) {
                    TimelineHit result =
                        hit(TimelineHitKind::SelectionStart, selected,
                            row->trackIndex, -1, frame);
                    result.lane = row->lane;
                    return result;
                }
                if (endHandle.contains(point)) {
                    TimelineHit result =
                        hit(TimelineHitKind::SelectionEnd, selected,
                            row->trackIndex, -1, frame);
                    result.lane = row->lane;
                    return result;
                }
            }
        }

        if (row->hasAudio) {
            const Rect& lane = row->lane;
            const Rect stretch{row->endX - 13, lane.y + 2, 11, 11};
            const Rect trimStart{row->startX - 4, lane.y + 2, 8, 14};
            const Rect trimEnd{row->endX - 4, lane.y + lane.h - 16, 8, 14};
            if (row->endX >= lane.x && row->endX <= lane.x + lane.w &&
                stretch.contains(point))
                {
                    TimelineHit result =
                        hit(TimelineHitKind::TrackStretch, lane,
                            row->trackIndex, -1, frame);
                    result.lane = lane;
                    return result;
                }
            if (row->startX >= lane.x && row->startX <= lane.x + lane.w &&
                trimStart.contains(point))
                {
                    TimelineHit result =
                        hit(TimelineHitKind::TrackTrimStart, lane,
                            row->trackIndex, -1, frame);
                    result.lane = lane;
                    return result;
                }
            if (row->endX >= lane.x && row->endX <= lane.x + lane.w &&
                trimEnd.contains(point))
                {
                    TimelineHit result =
                        hit(TimelineHitKind::TrackTrimEnd, lane,
                            row->trackIndex, -1, frame);
                    result.lane = lane;
                    return result;
                }
        }
        {
            TimelineHit result = hit(TimelineHitKind::Lane, row->lane,
                                     row->trackIndex, -1, frame);
            result.lane = row->lane;
            return result;
        }
    }

    if (m_area.contains(point))
        return hit(TimelineHitKind::WaveArea, m_area);
    return {};
}

} // namespace vwave::ui
