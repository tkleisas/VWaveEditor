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
        if (m_tab == vwave::EditorTab::Xenakis &&
            SDL_PointInRect(&clickPoint, &m_xenakisArea)) {
            m_xenakisPanning = true;
            m_xenakisPanStartX = event.x;
            m_xenakisPanStartView = xenakisViewStart();
            return;
        }
        m_panning = true;
        m_lastMouseX = event.x;
        return;
    }
    if (event.button == SDL_BUTTON_RIGHT &&
        m_tab == vwave::EditorTab::Xenakis) {
        handleXenakisMouseDown(event);
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
    } else if (m_tab == vwave::EditorTab::Xenakis) {
        handleXenakisMouseDown(event);
    }
}
