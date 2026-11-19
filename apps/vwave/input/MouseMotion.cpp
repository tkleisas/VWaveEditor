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
    updateUiCursor(event.x, event.y);
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
        handleSpectrumHover(event);
    }
    if (m_tab == vwave::EditorTab::Xenakis && m_xenakisPanning) {
        const double secondsPerPixel =
            xenakisViewDuration() /
            static_cast<double>(std::max(1, m_xenakisArea.w));
        const double pageDuration = std::max(
            0.001, m_xenakisController.page().duration);
        const double maxStart =
            std::max(0.0, pageDuration - xenakisViewDuration());
        m_xenakisViewStart = std::clamp(
            m_xenakisPanStartView -
                static_cast<double>(event.x - m_xenakisPanStartX) *
                    secondsPerPixel,
            0.0, maxStart);
        return;
    }
    if (m_tab == vwave::EditorTab::Xenakis &&
        (m_xenakisDrawing || m_xenakisDraggingPoint ||
         m_xenakisScrubbingPreview)) {
        handleXenakisMouseMotion(event);
        return;
    }
    if (m_waveDrag != WaveDragMode::None || m_panning || m_panningOverview) {
        handleWaveformMouseMotion(event);
        return;
    }
    if (m_draggingSpectrum) {
        handleSpectrumMouseMotion(event);
    }
    if (m_drawingWavetable && m_wavetableController.valid()) {
        handleWavetableMouseMotion(event);
    }
    if (m_scopeLoopDragging) {
        handleOscilloscopeMouseMotion(event);
    }
}
