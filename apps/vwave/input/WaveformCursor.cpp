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

    TimelineCanvasLayout layout;
    layout.area = fromSdl(m_waveArea);
    layout.ruler = fromSdl(m_rulerArea);
    layout.overview = fromSdl(m_overviewArea);
    layout.headerColumn = fromSdl(m_trackHeaderColumn);
    layout.trackScroll = static_cast<int>(std::lround(m_trackScroll));
    layout.clipToolEnabled = m_clips.toolEnabled();
    layout.laneArea = {layout.headerColumn.x + layout.headerColumn.w,
                       layout.headerColumn.y, 0, layout.headerColumn.h};
    for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
        if (m_trackHeaderRects[i].w <= 0 ||
            i >= m_trackLaneRects.size() || m_trackLaneRects[i].w <= 0)
            continue;
        layout.rowHeight = m_trackHeaderRects[i].h + 6;
        layout.laneArea = fromSdl(m_trackLaneRects[i]);
        layout.laneArea.y = layout.headerColumn.y;
        layout.laneArea.h = layout.headerColumn.h;
        break;
    }

    m_timelineCanvas.layout(m_timeline, m_view, layout);
    const auto& selection = m_timeline.selection();
    return m_timelineCanvas.cursorAt(
        {x, y}, selection, m_timeline.selectionScope(),
        m_timeline.activeTrack(), selection.active && selection.end > selection.start);
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
