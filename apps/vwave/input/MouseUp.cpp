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
        handleWaveformMouseUp(event);
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
        handleSpectrumMouseUp(event);
    }
}
