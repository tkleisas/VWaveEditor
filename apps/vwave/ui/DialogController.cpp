#include "ui/DialogController.h"

#include "ui/UiTheme.h"

#include <string>

#include <utility>

namespace vwave::ui {

namespace {

std::string fitText(Painter& painter, const std::string& text, int width) {
    if (painter.textWidth(text, FontRole::Normal) <= width) return text;
    std::string result = text;
    while (!result.empty() &&
           painter.textWidth(result + "...", FontRole::Normal) > width)
        result.pop_back();
    return result.empty() ? std::string{} : result + "...";
}

} // namespace

void DialogController::open(Mode mode, std::string title, std::string path,
                            bool save) {
    m_mode = mode;
    m_title = std::move(title);
    m_path = std::move(path);
    m_save = save;
    m_controls.resetInteraction();
    SDL_StartTextInput();
}

void DialogController::cancel() {
    m_mode = Mode::None;
    m_controls.resetInteraction();
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

void DialogController::draw(SDL_Renderer* renderer, Painter& painter,
                            int windowWidth, int windowHeight) {
    if (!isOpen() || !renderer) return;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_Rect overlay{0, 0, windowWidth, windowHeight};
    SDL_RenderFillRect(renderer, &overlay);

    const Rect box{windowWidth / 2 - 330, windowHeight / 2 - 95, 660, 190};
    painter.fillRect(box, ColorRole::Panel);
    painter.strokeRect(box, ColorRole::Accent);
    painter.drawText(m_title, {box.x + 20, box.y + 12, box.w - 40, 24},
                     ColorRole::Text, FontRole::Bold);

    const bool seekTime = m_mode == Mode::SeekTime;
    painter.drawText(seekTime ? "Time (seconds or MM:SS.mmm)" : "Path",
                     {box.x + 20, box.y + 50, box.w - 40, 18},
                     ColorRole::TextDim, FontRole::Small);
    const Rect field{box.x + 20, box.y + 78, box.w - 40, 34};
    painter.fillRect(field, ColorRole::Content);
    painter.strokeRect(field, ColorRole::Border);
    painter.drawText(fitText(painter, m_path, field.w - 20),
                     {field.x + 8, field.y, field.w - 16, field.h},
                     ColorRole::Text, FontRole::Normal);
    painter.drawText(
        seekTime ? "Enter to go | Esc to cancel | Ctrl+A clears input"
                 : "Enter to confirm | Esc to cancel | Ctrl+A clears path",
        {box.x + 20, box.y + 116, box.w - 40, 18}, ColorRole::TextFaint,
        FontRole::Small);

    m_controls.begin(box);
    std::string confirmLabel = m_save ? "Save" : "Open";
    if (m_mode == Mode::AddTrack) confirmLabel = "Add";
    if (m_mode == Mode::ExportSlices) confirmLabel = "Export";
    if (seekTime) confirmLabel = "Go";
    m_controls.addButton(
        confirmLabel, {box.x + box.w - 218, box.y + 150, 94, 28},
        [this] { confirm(); }, true);
    m_controls.addButton(
        "Cancel", {box.x + box.w - 114, box.y + 150, 94, 28},
        [this] { cancel(); });
    m_controls.end(&painter);
}

} // namespace vwave::ui
