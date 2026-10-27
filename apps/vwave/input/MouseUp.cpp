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
