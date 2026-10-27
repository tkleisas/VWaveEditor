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

void App::handleSpectrumMouseDown(const SDL_MouseButtonEvent& event) {
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

}

void App::handleSpectrumHover(const SDL_MouseMotionEvent& event) {
    SDL_Point point{event.x, event.y};
    if (SDL_PointInRect(&point, &m_spectrumArea)) {
        m_spectrum3DHoverX = event.x;
        m_spectrum3DHoverY = event.y;
    } else {
        m_spectrum3DHoverX = -1;
        m_spectrum3DHoverY = -1;
    }

}

void App::handleSpectrumMouseMotion(const SDL_MouseMotionEvent& event) {
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
}

void App::handleSpectrumMouseUp(const SDL_MouseButtonEvent& event) {
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
