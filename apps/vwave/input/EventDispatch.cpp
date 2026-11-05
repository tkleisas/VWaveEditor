#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

using namespace vwave::app;
using namespace vwave::ui;

bool App::handleToolbarInput(const SDL_Event& event) {
    if (!m_toolbarRoot) return false;
    const bool dragging = m_waveDrag != WaveDragMode::None || m_panning ||
                          m_panningOverview || m_shiftingTrack ||
                          m_draggingSpectrum ||
                          m_drawingWavetable;
    if (dragging && !m_toolbarHost.captured()) return false;
    const bool modal = m_dialog.isOpen() || m_settingsController.isOpen() ||
                       m_contextMenuOpen || m_openMenu >= 0;
    if (modal) {
        if (m_toolbarHost.captured() || m_toolbarHost.hovered()) {
            m_toolbarHost.handleEvent(
                WindowEvent{WindowEvent::Type::FocusLost});
            m_cursorController.apply(m_toolbarHost.cursor());
        }
        return false;
    }

    const std::optional<Event> translated = m_uiEventAdapter.translate(event);
    if (!translated) return false;

    if (const auto* pointer = std::get_if<PointerEvent>(&*translated)) {
        SDL_Point point{pointer->position.x, pointer->position.y};
        const bool inToolbar =
            SDL_PointInRect(&point, &m_toolbarRect) != 0;
        const bool captured = m_toolbarHost.captured() != nullptr;
        const bool hovered = m_toolbarHost.hovered() != nullptr;
        if (!inToolbar && !captured && !hovered) return false;

        m_toolbarHost.handleEvent(*translated);
        m_cursorController.apply(m_toolbarHost.cursor());
        if (event.type == SDL_MOUSEBUTTONDOWN ||
            event.type == SDL_MOUSEBUTTONUP ||
            event.type == SDL_MOUSEMOTION)
            return inToolbar || captured;
        return false;
    }

    if (std::holds_alternative<WindowEvent>(*translated)) {
        m_toolbarHost.handleEvent(*translated);
        m_cursorController.apply(m_toolbarHost.cursor());
    }
    return false;
}

bool App::handleTrackHeaderInput(const SDL_Event& event) {
    if (m_tab != vwave::EditorTab::Waveform) return false;
    const bool dragging = m_waveDrag != WaveDragMode::None || m_panning ||
                          m_panningOverview || m_shiftingTrack ||
                          m_draggingSpectrum ||
                          m_drawingWavetable;
    if (dragging && !m_trackHeaderControls.captured()) return false;

    const bool modal = m_dialog.isOpen() || m_settingsController.isOpen() ||
                       m_contextMenuOpen || m_openMenu >= 0;
    if (modal) {
        if (m_trackHeaderControls.captured() ||
            m_trackHeaderControls.hovered()) {
            m_trackHeaderControls.handleEvent(
                WindowEvent{WindowEvent::Type::FocusLost});
            m_cursorController.apply(m_trackHeaderControls.cursor());
        }
        return false;
    }

    const std::optional<Event> translated = m_uiEventAdapter.translate(event);
    if (!translated) return false;

    if (const auto* pointer = std::get_if<PointerEvent>(&*translated)) {
        SDL_Point point{pointer->position.x, pointer->position.y};
        const bool inHeader =
            SDL_PointInRect(&point, &m_trackHeaderColumn) != 0;
        const bool captured = m_trackHeaderControls.captured() != nullptr;
        const bool hovered = m_trackHeaderControls.hovered() != nullptr;
        if (!inHeader && !captured && !hovered) return false;

        const bool handled = m_trackHeaderControls.handleEvent(*translated);
        m_cursorController.apply(m_trackHeaderControls.cursor());
        if (event.type == SDL_MOUSEBUTTONDOWN ||
            event.type == SDL_MOUSEBUTTONUP)
            return handled;
        if (event.type == SDL_MOUSEMOTION)
            return handled || captured;
        return false;
    }

    if (std::holds_alternative<WindowEvent>(*translated)) {
        m_trackHeaderControls.handleEvent(*translated);
        m_cursorController.apply(m_trackHeaderControls.cursor());
    }
    return false;
}

bool App::handleSidebarInput(const SDL_Event& event) {
    if (!m_sidebarRoot) return false;
    const bool dragging = m_waveDrag != WaveDragMode::None || m_panning ||
                          m_panningOverview || m_shiftingTrack ||
                          m_draggingSpectrum ||
                          m_drawingWavetable;
    if (dragging && !m_sidebarHost.captured()) return false;

    const bool modal = m_dialog.isOpen() || m_settingsController.isOpen() ||
                       m_contextMenuOpen || m_openMenu >= 0;
    if (modal) {
        if (m_sidebarHost.captured() || m_sidebarHost.hovered()) {
            m_sidebarHost.handleEvent(
                WindowEvent{WindowEvent::Type::FocusLost});
            m_cursorController.apply(m_sidebarHost.cursor());
        }
        return false;
    }

    const std::optional<Event> translated = m_uiEventAdapter.translate(event);
    if (!translated) return false;

    if (const auto* pointer = std::get_if<PointerEvent>(&*translated)) {
        SDL_Point point{pointer->position.x, pointer->position.y};
        const bool inSidebar =
            SDL_PointInRect(&point, &m_sidebarRect) != 0;
        const bool captured = m_sidebarHost.captured() != nullptr;
        const bool hovered = m_sidebarHost.hovered() != nullptr;
        if (!inSidebar && !captured && !hovered) return false;

        const bool handled = m_sidebarHost.handleEvent(*translated);
        m_cursorController.apply(m_sidebarHost.cursor());
        if (event.type == SDL_MOUSEBUTTONDOWN ||
            event.type == SDL_MOUSEBUTTONUP)
            return handled;
        if (event.type == SDL_MOUSEMOTION)
            return handled || captured;
        return false;
    }

    if (std::holds_alternative<WindowEvent>(*translated)) {
        m_sidebarHost.handleEvent(*translated);
        m_cursorController.apply(m_sidebarHost.cursor());
    }
    return false;
}

void App::handleEvent(const SDL_Event& event) {
    if (handleToolbarInput(event)) return;
    if (handleSidebarInput(event)) return;
    if (handleTrackHeaderInput(event)) return;
    switch (event.type) {
    case SDL_QUIT:
        m_running = false;
        break;
    case SDL_DROPFILE: {
        const std::string path = event.drop.file;
        SDL_free(event.drop.file);
        if (m_dialog.isOpen()) break;
        const std::string extension =
            std::filesystem::path(path).extension().string();
        if (extension == ".vwdoc") loadProject(path);
        else if (isScopePath(path)) loadScopePathFile(path);
        else if (isImagePath(path)) loadImage(path);
        else if (m_timeline.empty()) loadAudio(path);
        else addTrackFile(path);
        break;
    }
    case SDL_KEYDOWN:
        handleKey(event.key);
        break;
    case SDL_TEXTINPUT:
        if (m_dialog.isOpen()) m_dialog.handleTextInput(event.text.text);
        break;
    case SDL_MOUSEBUTTONDOWN:
        handleMouseDown(event.button);
        break;
    case SDL_MOUSEBUTTONUP:
        handleMouseUp(event.button);
        break;
    case SDL_MOUSEMOTION:
        handleMouseMotion(event.motion);
        break;
    case SDL_MOUSEWHEEL:
        handleWheel(event.wheel);
        break;
    default:
        break;
    }
}

