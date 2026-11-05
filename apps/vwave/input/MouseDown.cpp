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
    if (m_settingsController.isOpen()) return;
    SDL_Point clickPoint{event.x, event.y};
    if (!SDL_PointInRect(&clickPoint, &m_contentRect)) return;
    if (event.button == SDL_BUTTON_MIDDLE) {
        m_panning = true;
        m_lastMouseX = event.x;
        return;
    }
    if (event.button != SDL_BUTTON_LEFT) return;
    if (m_tab == vwave::EditorTab::Waveform) {
        handleWaveformMouseDown(event);
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        handleSpectrumMouseDown(event);
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        handleWavetableMouseDown(event);
    } else if (m_tab == vwave::EditorTab::Oscilloscope) {
        handleOscilloscopeMouseDown(event);
    }
}
