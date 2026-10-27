#pragma once

#include "ui/UiRenderer.h"

#include <SDL.h>

#include <functional>
#include <string>
#include <utility>

namespace vwave::ui {

class DialogController {
public:
    enum class Mode {
        None,
        Open,
        AddTrack,
        SaveAudio,
        SaveProject,
        SaveWavetable,
        LoadWavetable,
        SaveMasteringReport,
        LoadImage,
        SaveScopePath,
        LoadScopePath,
        SeekTime,
        ExportSlices
    };

    using ConfirmHandler =
        std::function<void(Mode, const std::string&)>;

    void setConfirmHandler(ConfirmHandler handler) {
        m_confirm = std::move(handler);
    }

    bool isOpen() const { return m_mode != Mode::None; }
    Mode mode() const { return m_mode; }
    bool save() const { return m_save; }
    const std::string& path() const { return m_path; }

    void open(Mode mode, std::string title, std::string path, bool save);
    void cancel();
    void confirm();
    void handleTextInput(const std::string& text);
    void handleKey(const SDL_KeyboardEvent& event);
    bool handleMouseDown(int x, int y);
    void draw(UiRenderer& ui, int windowWidth, int windowHeight);

private:
    Mode m_mode = Mode::None;
    std::string m_path;
    std::string m_title;
    bool m_save = false;
    SDL_Rect m_confirmRect{};
    SDL_Rect m_cancelRect{};
    ConfirmHandler m_confirm;
};

} // namespace vwave::ui
