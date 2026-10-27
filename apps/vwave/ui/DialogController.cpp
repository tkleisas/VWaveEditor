#include "ui/DialogController.h"

#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <utility>

namespace vwave::ui {

void DialogController::open(Mode mode, std::string title, std::string path,
                            bool save) {
    m_mode = mode;
    m_title = std::move(title);
    m_path = std::move(path);
    m_save = save;
    SDL_StartTextInput();
}

void DialogController::cancel() {
    m_mode = Mode::None;
    SDL_StopTextInput();
}

void DialogController::confirm() {
    if (m_mode == Mode::None) return;
    const Mode mode = m_mode;
    const std::string path = m_path;
    cancel();
    if (m_confirm) m_confirm(mode, path);
}

void DialogController::handleTextInput(const std::string& text) {
    if (isOpen()) m_path += text;
}

void DialogController::handleKey(const SDL_KeyboardEvent& event) {
    if (!isOpen()) return;
    const SDL_Keycode key = event.keysym.sym;
    if (key == SDLK_ESCAPE) {
        cancel();
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        confirm();
    } else if (key == SDLK_BACKSPACE) {
        if (!m_path.empty()) {
            m_path.pop_back();
            while (!m_path.empty() &&
                   (static_cast<unsigned char>(m_path.back()) & 0xC0) == 0x80)
                m_path.pop_back();
        }
    } else if ((event.keysym.mod & KMOD_CTRL) != 0 && key == SDLK_a) {
        m_path.clear();
    }
}

bool DialogController::handleMouseDown(int x, int y) {
    if (!isOpen()) return false;
    const SDL_Point point{x, y};
    if (SDL_PointInRect(&point, &m_confirmRect)) {
        confirm();
    } else if (SDL_PointInRect(&point, &m_cancelRect)) {
        cancel();
    }
    return true;
}

void DialogController::draw(UiRenderer& ui, int windowWidth,
                            int windowHeight) {
    if (!isOpen()) return;
    SDL_Renderer* renderer = ui.renderer();
    if (!renderer) return;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_Rect overlay{0, 0, windowWidth, windowHeight};
    SDL_RenderFillRect(renderer, &overlay);

    SDL_Rect box{windowWidth / 2 - 330, windowHeight / 2 - 95, 660, 190};
    SDL_SetRenderDrawColor(renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, kAccent.r, kAccent.g, kAccent.b, 255);
    SDL_RenderDrawRect(renderer, &box);

    ui.drawText(m_title, box.x + 20, box.y + 18, kText, ui.fontBold());
    const bool seekTime = m_mode == Mode::SeekTime;
    ui.drawText(seekTime ? "Time (seconds or MM:SS.mmm)" : "Path",
                box.x + 20, box.y + 58, kTextDim, ui.fontSmall());
    SDL_Rect field{box.x + 20, box.y + 78, box.w - 40, 34};
    SDL_SetRenderDrawColor(renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(renderer, &field);
    SDL_SetRenderDrawColor(renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(renderer, &field);
    ui.drawText(truncateText(m_path, ui.fontNormal(), field.w - 20),
                field.x + 8, field.y + 9, kText, ui.fontNormal());
    ui.drawText(
        seekTime ? "Enter to go | Esc to cancel | Ctrl+A clears input"
                 : "Enter to confirm | Esc to cancel | Ctrl+A clears path",
        box.x + 20, box.y + 122, kTextFaint, ui.fontSmall());

    Button confirmButton;
    confirmButton.rect = {box.x + box.w - 218, box.y + 150, 94, 28};
    confirmButton.label = m_save ? "Save" : "Open";
    if (m_mode == Mode::AddTrack) confirmButton.label = "Add";
    if (m_mode == Mode::ExportSlices) confirmButton.label = "Export";
    if (seekTime) confirmButton.label = "Go";
    confirmButton.accent = true;
    m_confirmRect = confirmButton.rect;
    ui.drawButton(confirmButton);

    Button cancelButton;
    cancelButton.rect = {box.x + box.w - 114, box.y + 150, 94, 28};
    cancelButton.label = "Cancel";
    m_cancelRect = cancelButton.rect;
    ui.drawButton(cancelButton);
}

} // namespace vwave::ui
