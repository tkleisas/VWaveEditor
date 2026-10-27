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

void App::handleMouseDown(const SDL_MouseButtonEvent& event) {
    if (m_dialog.handleMouseDown(event.x, event.y)) return;
    if (m_settingsController.isOpen()) {
        if (event.button != SDL_BUTTON_LEFT) return;
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_settingsCloseRect)) m_settingsController.close();
        else if (SDL_PointInRect(&point, &m_settingsOutputPrev))
            m_settingsController.cycleOutputDevice(-1);
        else if (SDL_PointInRect(&point, &m_settingsOutputNext))
            m_settingsController.cycleOutputDevice(1);
        else if (SDL_PointInRect(&point, &m_settingsInputPrev))
            m_settingsController.cycleInputDevice(-1);
        else if (SDL_PointInRect(&point, &m_settingsInputNext))
            m_settingsController.cycleInputDevice(1);
        else if (SDL_PointInRect(&point, &m_settingsThemePrev))
            m_settingsController.setTheme(
            m_settingsController.settings().theme == "dark" ? "light"
                                                              : "dark");
        else if (SDL_PointInRect(&point, &m_settingsThemeNext))
            m_settingsController.setTheme(
            m_settingsController.settings().theme == "dark" ? "light"
                                                              : "dark");
        return;
    }
    if (event.button == SDL_BUTTON_RIGHT) {
        m_openMenu = -1;
        if (m_contextMenuOpen) {
            closeContextMenu();
            return;
        }
        if (m_tab == vwave::EditorTab::Waveform) {
            SDL_Point point{event.x, event.y};
            for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
                if (m_trackHeaderRects[i].w > 0 &&
                    SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
                    openTrackContextMenu(static_cast<int>(i), event.x,
                                         event.y);
                    return;
                }
            }
            for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
                if (m_trackLaneRects[i].w > 0 &&
                    SDL_PointInRect(&point, &m_trackLaneRects[i])) {
                    openTrackContextMenu(static_cast<int>(i), event.x,
                                         event.y);
                    return;
                }
            }
        }
        return;
    }
    if (m_contextMenuOpen) {
        if (event.button == SDL_BUTTON_LEFT) {
            SDL_Point point{event.x, event.y};
            if (SDL_PointInRect(&point, &m_contextMenuRect)) {
                for (size_t i = 0; i < m_contextMenuItemRects.size(); ++i) {
                    if (m_contextMenuItemRects[i].w > 0 &&
                        SDL_PointInRect(&point, &m_contextMenuItemRects[i]) &&
                        i < m_contextMenuItems.size() &&
                        m_contextMenuItems[i].enabled &&
                        !m_contextMenuItems[i].separator) {
                        auto action = m_contextMenuItems[i].action;
                        closeContextMenu();
                        if (action) action();
                        return;
                    }
                }
                return;
            }
            closeContextMenu();
        }
    }

    SDL_Point menuPoint{event.x, event.y};
    for (int index = 0; index < static_cast<int>(m_menus.size()); ++index) {
        if (SDL_PointInRect(&menuPoint, &m_menus[static_cast<size_t>(index)]
                                            .titleRect)) {
            m_openMenu = m_openMenu == index ? -1 : index;
            return;
        }
    }
    if (m_openMenu >= 0) {
        if (SDL_PointInRect(&menuPoint, &m_openMenuRect) && event.button ==
                                                               SDL_BUTTON_LEFT) {
            for (const auto& rect : m_menuItemRects) {
                if (rect.w > 0 && SDL_PointInRect(&menuPoint, &rect)) {
                    const size_t index = static_cast<size_t>(
                        &rect - m_menuItemRects.data());
                    MenuItem item = m_menus[static_cast<size_t>(m_openMenu)]
                                        .items[index];
                    m_openMenu = -1;
                    if (item.enabled && item.action) item.action();
                    return;
                }
            }
        }
        m_openMenu = -1;
        return;
    }
    if (event.button == SDL_BUTTON_LEFT) {
        SDL_Point point{event.x, event.y};
        const bool inSidebar = SDL_PointInRect(&point, &m_sidebarRect) != 0;
        const bool inToolbar = SDL_PointInRect(&point, &m_toolbarRect) != 0;
        const bool inContent = SDL_PointInRect(&point, &m_contentRect) != 0;
        const int area = inSidebar ? 1 : (inToolbar ? 0 : (inContent ? 2 : -1));
        if (area >= 0) {
            for (const auto& slider : m_ui.sliders()) {
                if (slider.area != area) continue;
                SDL_Rect hit{slider.track.x, slider.track.y - 12,
                             slider.track.w, slider.track.h + 20};
                if (SDL_PointInRect(&point, &hit)) {
                    m_activeSlider = slider;
                    m_sliderDragging = true;
                    setSliderFromX(m_activeSlider, event.x);
                    return;
                }
            }
        }
        if (area >= 0) {
            for (const auto& button : m_ui.buttons()) {
                if (button.enabled && button.area == area &&
                    SDL_PointInRect(&point, &button.rect)) {
                    button.action();
                    return;
                }
            }
        }
    }
    SDL_Point clickPoint{event.x, event.y};
    if (!SDL_PointInRect(&clickPoint, &m_contentRect)) return;
    if (event.button == SDL_BUTTON_MIDDLE) {
        m_panning = true;
        m_lastMouseX = event.x;
        return;
    }
    if (event.button != SDL_BUTTON_LEFT) return;
    if (m_tab == vwave::EditorTab::Waveform) {
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
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        if (m_spectrum3D) return;
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_spectrumArea)) {
            m_spectrumClickCandidate = false;
            const int64_t frame =
                laneFrameAtX(m_spectrumArea, event.x);
            if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                m_player.stop();
                m_playhead = clampFrame(frame, m_timeline.lengthFrames());
                m_timeline.clearSelection();
                m_spectralController.clear();
                m_waveDrag = WaveDragMode::LoopSelection;
                m_waveDragTrack = -1;
                m_waveDragMouseX = event.x;
                m_loop.beginDrag(m_playhead, m_spectrumArea);
                return;
            }
            m_spectrumClickCandidate = true;
            m_spectrumClickX = event.x;
            m_spectrumClickY = event.y;
            m_spectrumClickFrame = frame;
            m_draggingSpectrum = true;
            m_spectrumStartX = event.x;
            m_spectrumStartY = event.y;
            m_spectralController.clear();
        }
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_frameStripArea) &&
            m_wavetableController.valid()) {
            const int cellWidth = std::max(
                2, m_frameStripArea.w / std::max(1, m_wavetableController.table().frameCount()));
            const int index = (event.x - m_frameStripArea.x) / cellWidth;
            m_wavetableController.view().selectedFrame = index;
            m_wavetableController.view().clamp(m_wavetableController.table().frameCount());
        } else if (SDL_PointInRect(&point, &m_wavetableArea) &&
                   m_wavetableController.valid()) {
            m_drawingWavetable = true;
            m_lastWavetableSample = -1;
        }
    } else if (m_tab == vwave::EditorTab::Oscilloscope) {
        if (event.button != SDL_BUTTON_LEFT ||
            m_scopeController.shape() != 0)
            return;
        SDL_Point point{event.x, event.y};
        if (!SDL_PointInRect(&point, &m_scopeArea) || m_scopeArea.w <= 0 ||
            m_scopeArea.h <= 0)
            return;
        const double x =
            (static_cast<double>(event.x - m_scopeArea.x) /
             static_cast<double>(m_scopeArea.w)) *
                2.0 -
            1.0;
        const double y =
            1.0 -
            (static_cast<double>(event.y - m_scopeArea.y) /
             static_cast<double>(m_scopeArea.h)) *
                2.0;
        const vwave::ScopePoint candidate{std::clamp(x, -1.0, 1.0),
                                          std::clamp(y, -1.0, 1.0)};
        auto& path = m_scopeController.path();
        if (path.points.size() >= 3) {
            const auto& first = path.points.front();
            if (std::hypot(candidate.x - first.x, candidate.y - first.y) <
                0.06) {
                path.closed = true;
                setStatus("Scope path closed");
                return;
            }
        }
        path.name = "Path";
        path.points.push_back(candidate);
        path.closed = false;
        setStatus("Scope point " +
                  std::to_string(path.points.size()) +
                  " added");
    }
}


void App::handleMouseUp(const SDL_MouseButtonEvent& event) {
    if (event.button == SDL_BUTTON_MIDDLE) m_panning = false;
    m_panningOverview = false;
    m_drawingWavetable = false;
    m_lastWavetableSample = -1;
    m_sliderDragging = false;
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
    if (m_shiftingTrack) {
        m_shiftingTrack = false;
        m_shiftingTrackIndex = -1;
        clearAnalysis();
        m_view.clamp(m_timeline.lengthFrames());
        m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
        setStatus("Shifted track");
    }
    if (m_draggingSpectrum) {
        m_draggingSpectrum = false;
        if (m_spectrumClickCandidate) {
            m_player.stop();
            m_playhead = clampFrame(m_spectrumClickFrame,
                                    m_timeline.lengthFrames());
            m_timeline.clearSelection();
            m_spectralController.clear();
            setStatus("Playhead " +
                      formatSeconds(static_cast<double>(m_playhead) /
                                    std::max(1, m_timeline.sampleRate())));
        } else if (m_spectralController.region().frames.length() < 2 ||
                   m_spectralController.region().highHz <= m_spectralController.region().lowHz) {
            m_spectralController.region().active = false;
        } else {
            m_spectralLow.setValue(m_spectralController.region().lowHz);
            m_spectralHigh.setValue(m_spectralController.region().highHz);
        }
        m_spectrumClickCandidate = false;
    }
}


void App::handleMouseMotion(const SDL_MouseMotionEvent& event) {
    if (m_sliderDragging) {
        setSliderFromX(m_activeSlider, event.x);
        return;
    }
    if (m_shiftingTrack && m_shiftingTrackIndex >= 0 &&
        m_shiftingTrackIndex < m_timeline.trackCount()) {
        const double framesPerPixel =
            m_view.samplesPerPixel(std::max(1, m_waveArea.w));
        int64_t newStart = m_shiftStartFrame + static_cast<int64_t>(
            std::llround(static_cast<double>(event.x - m_shiftStartMouseX) *
                         framesPerPixel));
        newStart = std::max<int64_t>(0, newStart);
        m_timeline.setTrackStartFrame(m_shiftingTrackIndex, newStart);
        m_view.clamp(m_timeline.lengthFrames());
        return;
    }
    if (m_tab == vwave::EditorTab::Spectrum && m_spectrum3D) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_spectrumArea)) {
            m_spectrum3DHoverX = event.x;
            m_spectrum3DHoverY = event.y;
        } else {
            m_spectrum3DHoverX = -1;
            m_spectrum3DHoverY = -1;
        }
    }
    if (m_waveDrag != WaveDragMode::None) {
        const SDL_Rect& lane =
            m_waveDragTrack >= 0 &&
                    m_waveDragTrack < static_cast<int>(m_trackLaneRects.size()) &&
                    m_trackLaneRects[static_cast<size_t>(m_waveDragTrack)].w > 0
                ? m_trackLaneRects[static_cast<size_t>(m_waveDragTrack)]
                : m_waveArea;
        const int64_t frame = laneFrameAtX(lane, event.x);
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
    if (m_draggingSpectrum) {
        if (m_spectrumClickCandidate &&
            (std::abs(event.x - m_spectrumClickX) > 2 ||
             std::abs(event.y - m_spectrumClickY) > 2)) {
            m_spectrumClickCandidate = false;
        }
        const int x0 = std::clamp(m_spectrumStartX, m_spectrumArea.x,
                                  m_spectrumArea.x + m_spectrumArea.w);
        const int x1 = std::clamp(event.x, m_spectrumArea.x,
                                  m_spectrumArea.x + m_spectrumArea.w);
        const int y0 = std::clamp(m_spectrumStartY, m_spectrumArea.y,
                                  m_spectrumArea.y + m_spectrumArea.h);
        const int y1 = std::clamp(event.y, m_spectrumArea.y,
                                  m_spectrumArea.y + m_spectrumArea.h);
        if (std::abs(x1 - x0) >= 2 && std::abs(y1 - y0) >= 2 && hasTracks()) {
            const auto& track = m_timeline.track(m_timeline.activeTrack());
            const int64_t trackFrames = track.document.numFrames();
            auto timelineFrame = [this](int x) {
                const double fraction =
                    static_cast<double>(x - m_spectrumArea.x) /
                    std::max(1, m_spectrumArea.w);
                return static_cast<int64_t>(std::llround(
                    m_view.startFrame +
                    fraction * m_view.visibleFrames));
            };
            const int64_t local0 = std::clamp<int64_t>(
                timelineFrame(std::min(x0, x1)) - track.startFrame, 0,
                trackFrames);
            const int64_t local1 = std::clamp<int64_t>(
                timelineFrame(std::max(x0, x1)) - track.startFrame, 0,
                trackFrames);
            const double hz0 = m_spectralView.frequencyAtY(
                std::min(y0, y1) - m_spectrumArea.y, m_spectrumArea.h);
            const double hz1 = m_spectralView.frequencyAtY(
                std::max(y0, y1) - m_spectrumArea.y, m_spectrumArea.h);
            m_spectralController.region().frames = {std::min(local0, local1),
                                       std::max(local0, local1)};
            m_spectralController.region().lowHz = std::min(hz0, hz1);
            m_spectralController.region().highHz = std::max(hz0, hz1);
            m_spectralController.region().active =
                m_spectralController.region().frames.length() > 0;
        }
    }
    if (m_drawingWavetable && m_wavetableController.valid()) {
        const int64_t index = static_cast<int64_t>(
            static_cast<double>(event.x - m_wavetableArea.x) /
            std::max(1, m_wavetableArea.w) *
            (m_wavetableController.table().frameSize() - 1));
        const float value = std::clamp(
            1.0f - 2.0f * static_cast<float>(
                              event.y - m_wavetableArea.y) /
                              std::max(1, m_wavetableArea.h),
            -1.0f, 1.0f);
        auto& frame = m_wavetableController.table().frame(m_wavetableController.view().selectedFrame);
        const int clampedIndex = std::clamp(
            static_cast<int>(index), 0,
            static_cast<int>(frame.size()) - 1);
        if (m_lastWavetableSample >= 0 &&
            clampedIndex != m_lastWavetableSample) {
            const int start = std::min(m_lastWavetableSample, clampedIndex);
            const int end = std::max(m_lastWavetableSample, clampedIndex);
            const float startValue = frame[static_cast<size_t>(start)];
            for (int i = start; i <= end; ++i) {
                const float mix = static_cast<float>(i - start) /
                                  std::max(1, end - start);
                frame[static_cast<size_t>(i)] =
                    startValue + (value - startValue) * mix;
            }
        } else {
            frame[static_cast<size_t>(clampedIndex)] = value;
        }
        m_lastWavetableSample = clampedIndex;
    }
}


void App::handleWheel(const SDL_MouseWheelEvent& event) {
    if (m_dialog.isOpen()) return;
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    SDL_Point wheelPoint{mouseX, mouseY};
    if (SDL_PointInRect(&wheelPoint, &m_sidebarRect)) {
        const int maximum =
            std::max(0, m_sidebarContentHeight - m_sidebarRect.h);
        m_sidebarScroll = std::clamp(
            m_sidebarScroll - static_cast<double>(event.y) * 48.0, 0.0,
            static_cast<double>(maximum));
        return;
    }
    if (!SDL_PointInRect(&wheelPoint, &m_contentRect)) return;
    if (m_tab == vwave::EditorTab::Mastering ||
        m_tab == vwave::EditorTab::Image ||
        m_tab == vwave::EditorTab::Oscilloscope)
        return;
    if (m_tab == vwave::EditorTab::Spectrum) {
        if (m_spectrum3D) return;
        if (!SDL_PointInRect(&wheelPoint, &m_spectrumArea)) return;
        if ((SDL_GetModState() & KMOD_CTRL) != 0) {
            const double center = m_spectralView.frequencyAtY(
                mouseY - m_spectrumArea.y, m_spectrumArea.h);
            const double maxHz = std::min(
                20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
            const double factor = event.y > 0 ? 2.0 : 0.5;
            m_spectralView.zoom(factor, center, 20.0, maxHz);
            m_spectrogramDirty = true;
            return;
        }
        if (event.y == 0) return;
        const double factor = event.y > 0 ? 1.25 : 0.8;
        m_view.zoomAt(factor, mouseX - m_spectrumArea.x, m_spectrumArea.w,
                      m_timeline.lengthFrames());
        return;
    }
    if (m_tab == vwave::EditorTab::Waveform) {
        if (mouseX < m_tracksArea.x) {
            const int maximum =
                std::max(0, m_trackContentHeight - m_tracksArea.h);
            m_trackScroll = std::clamp(
                m_trackScroll - static_cast<double>(event.y) * 40.0, 0.0,
                static_cast<double>(maximum));
            return;
        }
        if (event.x != 0) {
            m_view.pan(static_cast<double>(event.x) * 30.0, m_waveArea.w,
                       m_timeline.lengthFrames());
            return;
        }
        if (event.y == 0) return;
        const double factor = event.y > 0 ? 1.25 : 0.8;
        m_view.zoomAt(factor, mouseX - m_waveArea.x, m_waveArea.w,
                      m_timeline.lengthFrames());
    }
}
