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
        handleWaveformMouseDown(event);
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        handleSpectrumMouseDown(event);
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        handleWavetableMouseDown(event);
    } else if (m_tab == vwave::EditorTab::Oscilloscope) {
        handleOscilloscopeMouseDown(event);
    }
}
