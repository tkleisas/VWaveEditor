#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace vwave::app;
using namespace vwave::ui;

void App::handleWaveformMouseDown(const SDL_MouseButtonEvent& event) {
    if (m_clips.toolEnabled() && !activeDocument().hasClips())
        m_clips.reset();
    m_clickCandidate = false;
    SDL_Point point{event.x, event.y};
    if (SDL_PointInRect(&point, &m_rulerArea)) {
        const int64_t frame =
            laneFrameAtX(m_rulerArea, event.x);
        m_player.stop();
        m_playhead = clampFrame(frame, m_timeline.lengthFrames());
        m_timeline.clearSelection();
        if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
            m_waveDrag = WaveDragMode::LoopSelection;
            m_waveDragTrack = -1;
            m_waveDragMouseX = event.x;
            m_loop.beginDrag(m_playhead, m_rulerArea);
        }
        return;
    }
    if ((SDL_GetModState() & KMOD_ALT) != 0) {
        for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
            if (m_trackHeaderRects[i].w > 0 &&
                SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
                selectTrack(static_cast<int>(i));
                m_shiftingTrack = true;
                m_shiftingTrackIndex = static_cast<int>(i);
                m_shiftStartMouseX = event.x;
                m_shiftStartFrame = m_timeline.track(
                    m_shiftingTrackIndex).startFrame;
                return;
            }
        }
        for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
            if (m_trackLaneRects[i].w > 0 &&
                SDL_PointInRect(&point, &m_trackLaneRects[i])) {
                selectTrack(static_cast<int>(i));
                m_shiftingTrack = true;
                m_shiftingTrackIndex = static_cast<int>(i);
                m_shiftStartMouseX = event.x;
                m_shiftStartFrame = m_timeline.track(
                    m_shiftingTrackIndex).startFrame;
                return;
            }
        }
    }
    for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
        if (m_trackHeaderRects[i].w > 0 &&
            SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
            selectTrack(static_cast<int>(i));
            return;
        }
    }
    int laneIndex = -1;
    for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
        if (m_trackLaneRects[i].w > 0 &&
            SDL_PointInRect(&point, &m_trackLaneRects[i])) {
            laneIndex = static_cast<int>(i);
            break;
        }
    }
    if (laneIndex >= 0) {
        selectTrack(laneIndex);
        const SDL_Rect& clipLane =
            m_trackLaneRects[static_cast<size_t>(laneIndex)];
        const auto& clipTrack = m_timeline.track(laneIndex);
        if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
            const int64_t frame =
                clampFrame(laneFrameAtX(clipLane, event.x),
                           m_timeline.lengthFrames());
            m_player.stop();
            m_playhead = frame;
            m_timeline.clearSelection();
            m_waveDrag = WaveDragMode::LoopSelection;
            m_waveDragTrack = laneIndex;
            m_waveDragMouseX = event.x;
            m_loop.beginDrag(frame, clipLane);
            return;
        }
        if (m_clips.toolEnabled()) {
            m_player.stop();
            m_playhead = clampFrame(laneFrameAtX(clipLane, event.x),
                                    m_timeline.lengthFrames());
            m_timeline.clearSelection();
            if (!clipTrack.document.hasClips()) {
                setStatus("Make clips first");
                return;
            }
            const int64_t local = std::clamp<int64_t>(
                laneFrameAtX(clipLane, event.x) - clipTrack.startFrame,
                0, clipTrack.document.numFrames());
            const int clipIndex =
                clipTrack.document.clipIndexAt(local);
            if (clipIndex < 0) {
                m_clips.setSelectedClip(-1);
                setStatus("Click inside a clip");
                return;
            }
            m_clips.setSelectedClip(clipIndex);
            const auto& clip =
                clipTrack.document.clips()[static_cast<size_t>(clipIndex)];
            const int64_t frames = vwave::clipOutputFrames(clip);
            const int64_t startGlobal =
                clipTrack.startFrame + clip.timelineStart;
            const int64_t endGlobal = startGlobal + frames;
            const int endX = laneXForFrame(clipLane, endGlobal);
            m_waveDragTrack = laneIndex;
            m_waveDragMouseX = event.x;
            m_waveDragOriginalStart = clip.timelineStart;
            m_waveDragOriginalEnd = clip.timelineStart + frames;
            m_clipDragOriginalFrames = frames;
            m_waveDragPreviewFrame = startGlobal;
            if (std::abs(event.x - endX) <= 8) {
                m_waveDrag = WaveDragMode::ClipStretch;
                m_waveDragPreviewFrame = endGlobal;
            } else {
                m_waveDrag = WaveDragMode::ClipMove;
            }
            setStatus("Selected " +
                      (clip.name.empty()
                           ? "clip " + std::to_string(clipIndex + 1)
                           : clip.name));
            return;
        }
        const SDL_Rect selection = selectionScreenRect();
        if (selection.w > 0) {
            SDL_Rect repeatHandle{selection.x + selection.w - 6,
                                  selection.y + selection.h - 12, 12, 12};
            if (SDL_PointInRect(&point, &repeatHandle)) {
                m_waveDrag = WaveDragMode::SelectionRepeat;
                m_waveDragTrack = m_timeline.activeTrack();
                m_waveDragMouseX = event.x;
                m_waveDragPreviewFrame = m_timeline.selection().end;
                m_repeatDragCount = 0;
                return;
            }
            SDL_Rect startHandle{selection.x - 4, selection.y, 9,
                                 selection.h};
            SDL_Rect endHandle{selection.x + selection.w - 5, selection.y,
                               9, selection.h};
            if (SDL_PointInRect(&point, &startHandle)) {
                m_waveDrag = WaveDragMode::SelectionStart;
                m_waveDragFixedFrame = m_timeline.selection().end;
                m_waveDragMouseX = event.x;
                return;
            }
            if (SDL_PointInRect(&point, &endHandle)) {
                m_waveDrag = WaveDragMode::SelectionEnd;
                m_waveDragFixedFrame = m_timeline.selection().start;
                m_waveDragMouseX = event.x;
                return;
            }
        }

        const SDL_Rect& lane =
            m_trackLaneRects[static_cast<size_t>(laneIndex)];
        const auto& track = m_timeline.track(laneIndex);
        const int64_t frames = track.document.numFrames();
        if (frames > 0) {
            const int startX = laneXForFrame(lane, track.startFrame);
            const int endX =
                laneXForFrame(lane, track.startFrame + frames);
            SDL_Rect stretchHandle{endX - 13, lane.y + 2, 11, 11};
            SDL_Rect trimStart{startX - 4, lane.y + 2, 8, 14};
            SDL_Rect trimEnd{endX - 4, lane.y + lane.h - 16, 8, 14};
            if (endX >= lane.x && endX <= lane.x + lane.w &&
                SDL_PointInRect(&point, &stretchHandle)) {
                m_waveDrag = WaveDragMode::TrackStretch;
                m_waveDragTrack = laneIndex;
                m_waveDragMouseX = event.x;
                m_waveDragOriginalStart = track.startFrame;
                m_waveDragOriginalEnd = track.startFrame + frames;
                m_waveDragPreviewFrame = m_waveDragOriginalEnd;
                return;
            }
            if (startX >= lane.x && startX <= lane.x + lane.w &&
                SDL_PointInRect(&point, &trimStart)) {
                m_waveDrag = WaveDragMode::TrackTrimStart;
                m_waveDragTrack = laneIndex;
                m_waveDragMouseX = event.x;
                m_waveDragOriginalStart = track.startFrame;
                m_waveDragOriginalEnd = track.startFrame + frames;
                m_waveDragPreviewFrame = m_waveDragOriginalStart;
                return;
            }
            if (endX >= lane.x && endX <= lane.x + lane.w &&
                SDL_PointInRect(&point, &trimEnd)) {
                m_waveDrag = WaveDragMode::TrackTrimEnd;
                m_waveDragTrack = laneIndex;
                m_waveDragMouseX = event.x;
                m_waveDragOriginalStart = track.startFrame;
                m_waveDragOriginalEnd = track.startFrame + frames;
                m_waveDragPreviewFrame = m_waveDragOriginalEnd;
                return;
            }
        }

        m_selectionAnchor = laneFrameAtX(lane, event.x);
        m_timeline.setSelection(m_selectionAnchor, m_selectionAnchor);
        m_clips.setSelectedClip(-1);
        m_waveDrag = WaveDragMode::NewSelection;
        m_waveDragTrack = laneIndex;
        m_waveDragMouseX = event.x;
        m_clickCandidate = true;
        m_clickFrame = m_selectionAnchor;
        m_clickMouseX = event.x;
        return;
    }
    if (SDL_PointInRect(&point, &m_overviewArea)) {
        const double fraction =
            static_cast<double>(event.x - m_overviewArea.x) /
            std::max(1, m_overviewArea.w);
        m_view.startFrame =
            fraction * m_timeline.lengthFrames() -
            m_view.visibleFrames / 2.0;
        m_view.clamp(m_timeline.lengthFrames());
        m_player.stop();
        m_playhead = clampFrame(
            static_cast<int64_t>(std::llround(
                fraction * static_cast<double>(
                               m_timeline.lengthFrames()))),
            m_timeline.lengthFrames());
        m_panningOverview = true;
        m_lastMouseX = event.x;
        return;
    }
    if (SDL_PointInRect(&point, &m_waveArea)) {
        m_selectionAnchor =
            m_view.frameAt(event.x - m_waveArea.x, m_waveArea.w,
                           m_timeline.lengthFrames());
        m_timeline.setSelection(m_selectionAnchor, m_selectionAnchor);
        m_clips.setSelectedClip(-1);
        m_waveDrag = WaveDragMode::NewSelection;
        m_waveDragTrack = m_timeline.activeTrack();
        m_waveDragMouseX = event.x;
        m_clickCandidate = true;
        m_clickFrame = m_selectionAnchor;
        m_clickMouseX = event.x;
    }

}
