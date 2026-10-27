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

void App::handleEvent(const SDL_Event& event) {
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

