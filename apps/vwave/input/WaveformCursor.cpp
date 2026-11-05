#include "App.h"

#include "ui/editor/TimelineCursor.h"

#include "vwave/ClipArrangement.h"

#include <algorithm>
#include <cstdlib>

using namespace vwave::ui;

namespace {

Rect fromSdl(const SDL_Rect& rect) {
    return {rect.x, rect.y, rect.w, rect.h};
}

} // namespace

CursorShape App::waveformCursorAt(int x, int y) const {
    if (m_tab != vwave::EditorTab::Waveform) return CursorShape::Arrow;

    TimelineCursorGeometry geometry;
    geometry.waveArea = fromSdl(m_waveArea);
    geometry.ruler = fromSdl(m_rulerArea);
    geometry.overview = fromSdl(m_overviewArea);
    geometry.clipToolEnabled = m_clips.toolEnabled();

    const size_t trackCount = static_cast<size_t>(m_timeline.trackCount());
    const size_t count =
        std::min(trackCount, std::min(m_trackHeaderRects.size(),
                                      m_trackLaneRects.size()));
    geometry.tracks.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& track = m_timeline.track(static_cast<int>(i));
        const SDL_Rect& header = m_trackHeaderRects[i];
        const SDL_Rect& lane = m_trackLaneRects[i];
        const int64_t frames = track.document.numFrames();
        const bool hasAudio = !track.empty && frames > 0;
        TimelineTrackVisual visual;
        visual.header = fromSdl(header);
        visual.lane = fromSdl(lane);
        visual.trackIndex = static_cast<int>(i);
        visual.startX = laneXForFrame(lane, track.startFrame);
        visual.endX = laneXForFrame(lane, track.startFrame + frames);
        visual.hasAudio = hasAudio;
        geometry.tracks.push_back(visual);

        if (!geometry.clipToolEnabled || track.document.clips().empty())
            continue;
        const auto& clips = track.document.clips();
        for (size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex) {
            const auto& clip = clips[clipIndex];
            const int64_t clipFrames = vwave::clipOutputFrames(clip);
            const int64_t globalStart =
                track.startFrame + clip.timelineStart;
            const int left = laneXForFrame(lane, globalStart);
            const int right = laneXForFrame(lane, globalStart + clipFrames);
            geometry.clips.push_back(
                {{std::min(left, right), lane.y, std::abs(right - left),
                  lane.h},
                 static_cast<int>(i), static_cast<int>(clipIndex)});
        }
    }

    if (hasTracks()) {
        const SDL_Rect selection = selectionScreenRect();
        geometry.selection = fromSdl(selection);
        geometry.selectionActive = selection.w > 0 && selection.h > 0;
    }
    return timelineCursorAt(geometry, {x, y});
}

void App::updateUiCursor(int x, int y) {
    if (m_dialog.isOpen() || m_settingsController.isOpen() ||
        m_contextMenuOpen || m_openMenu >= 0) {
        m_cursorController.apply(CursorShape::Arrow);
        return;
    }

    CursorShape shape = CursorShape::Arrow;
    if (m_panning || m_panningOverview) {
        shape = CursorShape::ClosedHand;
    } else if (m_shiftingTrack) {
        shape = CursorShape::SizeAll;
    } else if (m_waveDrag != WaveDragMode::None) {
        switch (m_waveDrag) {
        case WaveDragMode::ClipMove:
        case WaveDragMode::SelectionRepeat:
            shape = CursorShape::SizeAll;
            break;
        case WaveDragMode::SelectionStart:
        case WaveDragMode::SelectionEnd:
        case WaveDragMode::TrackTrimStart:
        case WaveDragMode::TrackTrimEnd:
        case WaveDragMode::TrackStretch:
        case WaveDragMode::ClipStretch:
        case WaveDragMode::LoopSelection:
            shape = CursorShape::SizeWE;
            break;
        case WaveDragMode::NewSelection:
            shape = CursorShape::Crosshair;
            break;
        case WaveDragMode::None:
            break;
        }
    } else if (m_draggingSpectrum) {
        shape = CursorShape::Crosshair;
    } else if (m_drawingWavetable && m_wavetableController.valid()) {
        shape = CursorShape::Crosshair;
    } else if (m_tab == vwave::EditorTab::Waveform) {
        shape = waveformCursorAt(x, y);
    }
    m_cursorController.apply(shape);
}
