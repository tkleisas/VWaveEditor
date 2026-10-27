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
