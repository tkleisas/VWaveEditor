#pragma once

#include "ui/editor/ControlPanel.h"
#include "ui/framework/Painter.h"

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
        ImportScopeImage,
        ImportScopeSvg,
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
    bool handleEvent(const Event& event) { return m_controls.handleEvent(event); }
    CursorShape cursor() const { return m_controls.cursor(); }
    const std::vector<PushButton*>& buttons() const {
        return m_controls.buttons();
    }
    void draw(SDL_Renderer* renderer, Painter& painter, int windowWidth,
              int windowHeight);

private:
    Mode m_mode = Mode::None;
    std::string m_path;
    std::string m_title;
    bool m_save = false;
    ControlPanel m_controls;
    ConfirmHandler m_confirm;
};

} // namespace vwave::ui
