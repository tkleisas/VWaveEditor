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
