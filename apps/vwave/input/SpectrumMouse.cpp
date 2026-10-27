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
