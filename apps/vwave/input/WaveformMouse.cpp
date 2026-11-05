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
    refreshTimelineCanvas();

    const auto& selection = m_timeline.selection();
    const bool selectionActive =
        selection.active && selection.end > selection.start;
    const TimelineHit hit = m_timelineCanvas.hitTest(
        {event.x, event.y}, selection, m_timeline.selectionScope(),
        m_timeline.activeTrack(), selectionActive);

    auto beginShift = [this, &event](int trackIndex) {
        selectTrack(trackIndex);
        m_shiftingTrack = true;
        m_shiftingTrackIndex = trackIndex;
        m_shiftStartMouseX = event.x;
        m_shiftStartFrame = m_timeline.track(trackIndex).startFrame;
    };

    if (hit.kind == TimelineHitKind::Ruler) {
        m_player.stop();
        m_playhead = clampFrame(hit.frame, m_timeline.lengthFrames());
        m_timeline.clearSelection();
        if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
            m_waveDrag = WaveDragMode::LoopSelection;
            m_waveDragTrack = -1;
            m_waveDragMouseX = event.x;
            m_loop.beginDrag(m_playhead, m_rulerArea);
        }
        return;
    }

    if (hit.kind == TimelineHitKind::Overview) {
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

    if (hit.kind == TimelineHitKind::TrackHeader) {
        if ((SDL_GetModState() & KMOD_ALT) != 0) {
            beginShift(hit.trackIndex);
            return;
        }
        selectTrack(hit.trackIndex);
        return;
    }

    if (hit.kind == TimelineHitKind::Lane ||
        hit.kind == TimelineHitKind::SelectionStart ||
        hit.kind == TimelineHitKind::SelectionEnd ||
        hit.kind == TimelineHitKind::SelectionRepeat ||
        hit.kind == TimelineHitKind::ClipBody ||
        hit.kind == TimelineHitKind::ClipEnd ||
        hit.kind == TimelineHitKind::TrackTrimStart ||
        hit.kind == TimelineHitKind::TrackTrimEnd ||
        hit.kind == TimelineHitKind::TrackStretch) {
        selectTrack(hit.trackIndex);
        if ((SDL_GetModState() & KMOD_ALT) != 0) {
            beginShift(hit.trackIndex);
            return;
        }

        const SDL_Rect lane{hit.lane.x, hit.lane.y, hit.lane.w, hit.lane.h};
        if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
            m_player.stop();
            m_playhead = clampFrame(hit.frame, m_timeline.lengthFrames());
            m_timeline.clearSelection();
            m_waveDrag = WaveDragMode::LoopSelection;
            m_waveDragTrack = hit.trackIndex;
            m_waveDragMouseX = event.x;
            m_loop.beginDrag(m_playhead, lane);
            return;
        }

        if (m_clips.toolEnabled()) {
            const auto& track = m_timeline.track(hit.trackIndex);
            m_player.stop();
            m_playhead = clampFrame(hit.frame, m_timeline.lengthFrames());
            m_timeline.clearSelection();
            if (!track.document.hasClips()) {
                setStatus("Make clips first");
                return;
            }
            if (hit.kind != TimelineHitKind::ClipBody &&
                hit.kind != TimelineHitKind::ClipEnd) {
                m_clips.setSelectedClip(-1);
                setStatus("Click inside a clip");
                return;
            }
            if (hit.clipIndex < 0 ||
                static_cast<size_t>(hit.clipIndex) >=
                    track.document.clips().size()) {
                m_clips.setSelectedClip(-1);
                setStatus("Click inside a clip");
                return;
            }
            m_clips.setSelectedClip(hit.clipIndex);
            const auto& clip =
                track.document.clips()[static_cast<size_t>(hit.clipIndex)];
            const int64_t frames = vwave::clipOutputFrames(clip);
            const int64_t startGlobal =
                track.startFrame + clip.timelineStart;
            const int64_t endGlobal = startGlobal + frames;
            m_waveDragTrack = hit.trackIndex;
            m_waveDragMouseX = event.x;
            m_waveDragOriginalStart = clip.timelineStart;
            m_waveDragOriginalEnd = clip.timelineStart + frames;
            m_clipDragOriginalFrames = frames;
            m_waveDragPreviewFrame = startGlobal;
            if (hit.kind == TimelineHitKind::ClipEnd) {
                m_waveDrag = WaveDragMode::ClipStretch;
                m_waveDragPreviewFrame = endGlobal;
            } else {
                m_waveDrag = WaveDragMode::ClipMove;
            }
            setStatus("Selected " +
                      (clip.name.empty()
                           ? "clip " + std::to_string(hit.clipIndex + 1)
                           : clip.name));
            return;
        }

        if (hit.kind == TimelineHitKind::SelectionRepeat) {
            m_waveDrag = WaveDragMode::SelectionRepeat;
            m_waveDragTrack = m_timeline.activeTrack();
            m_waveDragMouseX = event.x;
            m_waveDragPreviewFrame = selection.end;
            m_repeatDragCount = 0;
            return;
        }
        if (hit.kind == TimelineHitKind::SelectionStart) {
            m_waveDrag = WaveDragMode::SelectionStart;
            m_waveDragFixedFrame = selection.end;
            m_waveDragMouseX = event.x;
            return;
        }
        if (hit.kind == TimelineHitKind::SelectionEnd) {
            m_waveDrag = WaveDragMode::SelectionEnd;
            m_waveDragFixedFrame = selection.start;
            m_waveDragMouseX = event.x;
            return;
        }

        const auto& track = m_timeline.track(hit.trackIndex);
        const int64_t frames = track.document.numFrames();
        if (hit.kind == TimelineHitKind::TrackStretch) {
            m_waveDrag = WaveDragMode::TrackStretch;
            m_waveDragTrack = hit.trackIndex;
            m_waveDragMouseX = event.x;
            m_waveDragOriginalStart = track.startFrame;
            m_waveDragOriginalEnd = track.startFrame + frames;
            m_waveDragPreviewFrame = m_waveDragOriginalEnd;
            return;
        }
        if (hit.kind == TimelineHitKind::TrackTrimStart) {
            m_waveDrag = WaveDragMode::TrackTrimStart;
            m_waveDragTrack = hit.trackIndex;
            m_waveDragMouseX = event.x;
            m_waveDragOriginalStart = track.startFrame;
            m_waveDragOriginalEnd = track.startFrame + frames;
            m_waveDragPreviewFrame = m_waveDragOriginalStart;
            return;
        }
        if (hit.kind == TimelineHitKind::TrackTrimEnd) {
            m_waveDrag = WaveDragMode::TrackTrimEnd;
            m_waveDragTrack = hit.trackIndex;
            m_waveDragMouseX = event.x;
            m_waveDragOriginalStart = track.startFrame;
            m_waveDragOriginalEnd = track.startFrame + frames;
            m_waveDragPreviewFrame = m_waveDragOriginalEnd;
            return;
        }

        m_selectionAnchor = hit.frame;
        m_timeline.setSelection(m_selectionAnchor, m_selectionAnchor);
        m_clips.setSelectedClip(-1);
        m_waveDrag = WaveDragMode::NewSelection;
        m_waveDragTrack = hit.trackIndex;
        m_waveDragMouseX = event.x;
        m_clickCandidate = true;
        m_clickFrame = m_selectionAnchor;
        m_clickMouseX = event.x;
        return;
    }

    if (hit.kind == TimelineHitKind::WaveArea) {
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

void App::handleWaveformMouseMotion(const SDL_MouseMotionEvent& event) {
if (m_waveDrag != WaveDragMode::None) {
    const TimelineCanvasRow* dragRow =
        m_waveDragTrack >= 0 ? m_timelineCanvas.rowForTrack(m_waveDragTrack)
                             : nullptr;
    const SDL_Rect lane = dragRow && dragRow->lane.w > 0
                              ? SDL_Rect{dragRow->lane.x, dragRow->lane.y,
                                         dragRow->lane.w, dragRow->lane.h}
                              : m_waveArea;
    const int64_t frame =
        dragRow && dragRow->lane.w > 0
            ? m_timelineCanvas.frameAtX(dragRow->lane, event.x)
            : laneFrameAtX(lane, event.x);
    const int64_t looseFrame = std::max<int64_t>(
        0, static_cast<int64_t>(std::llround(
               m_view.startFrame +
               static_cast<double>(event.x - lane.x) *
                   m_view.samplesPerPixel(std::max(1, lane.w)))));
    switch (m_waveDrag) {
    case WaveDragMode::NewSelection:
        if (m_clickCandidate &&
            std::abs(event.x - m_clickMouseX) > 2)
            m_clickCandidate = false;
        m_timeline.setSelection(std::min(m_selectionAnchor, frame),
                                std::max(m_selectionAnchor, frame));
        break;
    case WaveDragMode::LoopSelection: {
        const int64_t loopFrame =
            m_loop.dragArea().w > 0
                ? laneFrameAtX(m_loop.dragArea(), event.x)
                : frame;
        m_loop.updateDrag(loopFrame);
        m_clickCandidate = false;
        break;
    }
    case WaveDragMode::SelectionStart:
        m_timeline.setSelection(
            std::min(frame, m_waveDragFixedFrame),
            std::max(frame, m_waveDragFixedFrame));
        break;
    case WaveDragMode::SelectionEnd:
        m_timeline.setSelection(
            std::min(frame, m_waveDragFixedFrame),
            std::max(frame, m_waveDragFixedFrame));
        break;
    case WaveDragMode::TrackTrimStart:
        m_waveDragPreviewFrame =
            std::clamp<int64_t>(frame, m_waveDragOriginalStart,
                                m_waveDragOriginalEnd - 1);
        break;
    case WaveDragMode::TrackTrimEnd:
        m_waveDragPreviewFrame =
            std::clamp<int64_t>(frame, m_waveDragOriginalStart + 1,
                                m_waveDragOriginalEnd);
        break;
    case WaveDragMode::TrackStretch: {
        const int64_t originalLength =
            m_waveDragOriginalEnd - m_waveDragOriginalStart;
        m_waveDragPreviewFrame = std::clamp<int64_t>(
            looseFrame, m_waveDragOriginalStart + 1,
            m_waveDragOriginalStart + originalLength * 20);
        break;
    }
    case WaveDragMode::SelectionRepeat: {
        const auto& selection = m_timeline.selection();
        if (selection.active && selection.length() > 0) {
            const double repeats =
                static_cast<double>(looseFrame - selection.end) /
                static_cast<double>(selection.length());
            m_repeatDragCount = std::clamp(
                static_cast<int>(std::floor(repeats)), 0, 64);
            m_waveDragPreviewFrame = selection.end + m_repeatDragCount *
                                                      selection.length();
        }
        break;
    }
    case WaveDragMode::ClipMove: {
        if (m_waveDragTrack >= 0 &&
            m_waveDragTrack < m_timeline.trackCount()) {
            const auto& track =
                m_timeline.track(m_waveDragTrack);
            const double framesPerPixel =
                m_view.samplesPerPixel(std::max(1, lane.w));
            const int64_t delta = static_cast<int64_t>(std::llround(
                static_cast<double>(event.x - m_waveDragMouseX) *
                framesPerPixel));
            const int64_t localStart =
                std::max<int64_t>(0, m_waveDragOriginalStart + delta);
            m_waveDragPreviewFrame =
                track.startFrame + localStart;
        }
        break;
    }
    case WaveDragMode::ClipStretch: {
        if (m_waveDragTrack >= 0 &&
            m_waveDragTrack < m_timeline.trackCount() &&
            m_clipDragOriginalFrames > 0) {
            const auto& track =
                m_timeline.track(m_waveDragTrack);
            const double framesPerPixel =
                m_view.samplesPerPixel(std::max(1, lane.w));
            const int64_t delta = static_cast<int64_t>(std::llround(
                static_cast<double>(event.x - m_waveDragMouseX) *
                framesPerPixel));
            const int64_t maximum =
                std::max<int64_t>(1, m_clipDragOriginalFrames * 20);
            const int64_t frames = std::clamp<int64_t>(
                m_clipDragOriginalFrames + delta, 1, maximum);
            m_waveDragPreviewFrame =
                track.startFrame + m_waveDragOriginalStart + frames;
        }
        break;
    }
    case WaveDragMode::None:
        break;
    }
    return;
}
if (m_panning) {
    const int delta = event.x - m_lastMouseX;
    m_view.pan(-delta, m_waveArea.w, m_timeline.lengthFrames());
    m_lastMouseX = event.x;
}
if (m_panningOverview) {
    const double fraction =
        static_cast<double>(event.x - m_overviewArea.x) /
        std::max(1, m_overviewArea.w);
    m_view.startFrame = fraction * m_timeline.lengthFrames() -
                        m_view.visibleFrames / 2.0;
    m_view.clamp(m_timeline.lengthFrames());
    m_lastMouseX = event.x;
}
}

void App::handleWaveformMouseUp(const SDL_MouseButtonEvent& event) {
if (m_waveDrag != WaveDragMode::None) {
    const WaveDragMode drag = m_waveDrag;
    const int index = m_waveDragTrack;
    const int64_t preview = m_waveDragPreviewFrame;
    const int repeatCount = m_repeatDragCount;
    const int64_t originalStart = m_waveDragOriginalStart;
    const int64_t originalEnd = m_waveDragOriginalEnd;
    const int64_t clipOriginalFrames = m_clipDragOriginalFrames;
    m_waveDrag = WaveDragMode::None;
    m_waveDragTrack = -1;
    m_repeatDragCount = 0;
    m_clipDragOriginalFrames = 0;
    if (drag == WaveDragMode::TrackTrimStart ||
        drag == WaveDragMode::TrackTrimEnd) {
        if (index >= 0 && index < m_timeline.trackCount() &&
            preview != (drag == WaveDragMode::TrackTrimStart
                            ? originalStart
                            : originalEnd)) {
            const vwave::Range range =
                drag == WaveDragMode::TrackTrimStart
                    ? vwave::Range{preview, originalEnd}
                    : vwave::Range{originalStart, preview};
            m_player.stop();
            std::string error;
            if (m_timeline.cropTrack(index, range, &error)) {
                markEdited();
                setStatus("Trimmed track");
            } else {
                setStatus(error.empty() ? "Trim failed" : error, 6000);
            }
        }
    } else if (drag == WaveDragMode::TrackStretch) {
        if (index >= 0 && index < m_timeline.trackCount() &&
            originalEnd > originalStart) {
            const double ratio =
                static_cast<double>(preview - originalStart) /
                static_cast<double>(originalEnd - originalStart);
            if (std::isfinite(ratio) && ratio > 0.0 &&
                std::abs(ratio - 1.0) > 1e-3) {
                m_player.stop();
                std::string error;
                if (m_timeline.track(index).document.timeStretchTrack(
                        ratio, selectedStretchAlgorithm(), &error)) {
                    markEdited();
                    setStatus("Stretched track to " +
                              formatRatio(ratio));
                } else {
                    setStatus(error.empty() ? "Stretch failed" : error,
                              6000);
                }
            }
        }
    } else if (drag == WaveDragMode::ClipMove) {
        if (index >= 0 && index < m_timeline.trackCount() &&
            m_clips.selectedClip() >= 0) {
            auto& track = m_timeline.track(index);
            const int64_t localStart =
                std::max<int64_t>(0, preview - track.startFrame);
            if (localStart != originalStart) {
                m_player.stop();
                std::string error;
                if (track.document.moveClip(
                        static_cast<size_t>(m_clips.selectedClip()),
                        localStart,
                        &error)) {
                    markEdited();
                    setStatus("Moved clip");
                } else {
                    setStatus(error.empty() ? "Move failed" : error, 6000);
                }
            }
        }
    } else if (drag == WaveDragMode::ClipStretch) {
        if (index >= 0 && index < m_timeline.trackCount() &&
            m_clips.selectedClip() >= 0 && clipOriginalFrames > 0) {
            auto& track = m_timeline.track(index);
            const int64_t clipStartGlobal =
                track.startFrame + originalStart;
            const int64_t newFrames = preview - clipStartGlobal;
            const double ratio =
                static_cast<double>(newFrames) /
                static_cast<double>(clipOriginalFrames);
            if (std::isfinite(ratio) && ratio > 0.0 &&
                std::abs(ratio - 1.0) > 1e-3) {
                m_player.stop();
                std::string error;
                if (track.document.stretchClip(
                        static_cast<size_t>(m_clips.selectedClip()), ratio,
                        &error)) {
                    markEdited();
                    setStatus("Stretched clip");
                } else {
                    setStatus(error.empty() ? "Stretch failed" : error,
                              6000);
                }
            }
        }
    } else if (drag == WaveDragMode::SelectionRepeat) {
        if (repeatCount > 0) repeatActiveSelection(repeatCount);
    } else if (drag == WaveDragMode::LoopSelection) {
        m_loop.finishDrag(m_timeline.lengthFrames());
        if (!m_loop.active(m_timeline.lengthFrames())) {
            clearLoop();
        } else {
            setStatus("Loop " +
                      formatSeconds(static_cast<double>(m_loop.start()) /
                                    std::max(1, m_timeline.sampleRate())) +
                      " - " +
                      formatSeconds(static_cast<double>(m_loop.end()) /
                                    std::max(1, m_timeline.sampleRate())));
        }
    } else if (drag == WaveDragMode::NewSelection &&
               m_clickCandidate) {
        m_player.stop();
        m_playhead = clampFrame(m_clickFrame,
                                m_timeline.lengthFrames());
        m_timeline.clearSelection();
        setStatus("Playhead " +
                  formatSeconds(static_cast<double>(m_playhead) /
                                std::max(1, m_timeline.sampleRate())));
    }
    m_clickCandidate = false;
}
}
