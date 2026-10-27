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

void App::handleKey(const SDL_KeyboardEvent& event) {
    const SDL_Keycode key = event.keysym.sym;
    if (m_dialog.isOpen()) {
        m_dialog.handleKey(event);
        return;
    }
    if (m_settingsController.isOpen()) {
        if (key == SDLK_ESCAPE) m_settingsController.close();
        return;
    }
    if (key == SDLK_ESCAPE && m_contextMenuOpen) {
        closeContextMenu();
        return;
    }
    if (key == SDLK_ESCAPE && m_openMenu >= 0) {
        m_openMenu = -1;
        return;
    }
    const bool ctrl = (event.keysym.mod & KMOD_CTRL) != 0;
    const bool shift = (event.keysym.mod & KMOD_SHIFT) != 0;
    if (ctrl && key == SDLK_COMMA) {
        m_settingsController.open();
    } else if (ctrl && key == SDLK_o) {
        openDialog(DialogMode::Open, "Open audio, project, or image", "", false);
    } else if (ctrl && shift && key == SDLK_s) {
        openDialog(DialogMode::SaveProject, "Save project", m_projectPath,
                   true);
    } else if (ctrl && key == SDLK_s) {
        openDialog(DialogMode::SaveAudio, "Export mixdown",
                   suggestedMixdownPath(m_projectPath, m_audioPath), true);
    } else if (ctrl && key == SDLK_q) {
        m_running = false;
    } else if (ctrl && key == SDLK_z) {
        m_player.stop();
        if (activeDocument().undo()) {
            markEdited();
            setStatus("Undone");
        } else {
            setStatus("Nothing to undo");
        }
    } else if (ctrl && (key == SDLK_y ||
                        (shift && key == SDLK_z))) {
        m_player.stop();
        if (activeDocument().redo()) {
            markEdited();
            setStatus("Redone");
        } else {
            setStatus("Nothing to redo");
        }
    } else if (ctrl && key == SDLK_a) {
        m_timeline.setSelection(0, m_timeline.lengthFrames());
        setStatus("Selected everything");
    } else if (ctrl && key == SDLK_c) {
        setStatus(activeDocument().copySelection() ? "Copied selection"
                                             : "Select audio first");
    } else if (ctrl && key == SDLK_x) {
        runEdit([this](std::string* error) {
            return activeDocument().cutSelection(error);
        }, "Cut selection");
    } else if (ctrl && key == SDLK_v) {
        runEdit([this](std::string* error) {
            return activeDocument().paste(error);
        }, "Pasted");
    } else if (key == SDLK_HOME) {
        goToStart();
    } else if (key == SDLK_END) {
        goToEnd();
    } else if (key == SDLK_g) {
        openDialog(
            DialogMode::SeekTime, "Go to time",
            formatTimeInput(static_cast<double>(m_playhead) /
                            std::max(1, m_timeline.sampleRate())),
            false);
    } else if (key == SDLK_SPACE) {
        togglePlay();
    } else if (key == SDLK_ESCAPE) {
        if (m_spectralController.region().active) {
            m_spectralController.clear();
            setStatus("Spectral selection cleared");
        } else {
            m_timeline.clearSelection();
            setStatus("Selection cleared");
        }
    } else if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
        runEdit([this](std::string* error) {
            return activeDocument().deleteSelection(error);
        }, "Selection deleted");
    } else if (key == SDLK_l) {
        if (shift)
            setLoopToSelection();
        else
            toggleLoop();
    } else if (key == SDLK_1) {
        setTab(vwave::EditorTab::Waveform);
    } else if (key == SDLK_2) {
        setTab(vwave::EditorTab::Spectrum);
    } else if (key == SDLK_3) {
        setTab(vwave::EditorTab::Wavetable);
    } else if (key == SDLK_4) {
        setTab(vwave::EditorTab::Mastering);
    } else if (key == SDLK_5) {
        setTab(vwave::EditorTab::Image);
    } else if (key == SDLK_6) {
        setTab(vwave::EditorTab::Oscilloscope);
    } else if (key == SDLK_b) {
        analyzeBeats();
    } else if (key == SDLK_p) {
        detectPitch();
    } else if (key == SDLK_t) {
        sliceCurrent();
    } else if (key == SDLK_m) {
        addMarkerAtPlayhead();
    } else if (key == SDLK_F9) {
        toggleRecording();
    } else if (key == SDLK_w) {
        extractWavetable();
    } else if (key == SDLK_e) {
        openDialog(DialogMode::ExportSlices, "Export slices to directory",
                   (std::filesystem::current_path() / "slices").string(),
                   true);
    } else if (key == SDLK_r) {
        runEdit([this](std::string* error) {
            return activeDocument().reverseSelection(error);
        }, "Reversed");
    } else if (key == SDLK_i) {
        runEdit([this](std::string* error) {
            return activeDocument().invertSelection(error);
        }, "Polarity inverted");
    } else if (key == SDLK_n) {
        runEdit([this](std::string* error) {
            return activeDocument().normalize(-1.0, error);
        }, "Normalized to -1 dBFS");
    } else if (key == SDLK_f) {
        runEdit([this, shift](std::string* error) {
            return shift ? activeDocument().fadeOut(error)
                         : activeDocument().fadeIn(error);
        }, shift ? "Fade out applied" : "Fade in applied");
    } else if (key == SDLK_PLUS || key == SDLK_EQUALS ||
               key == SDLK_KP_PLUS) {
        zoomTime(1.25);
    } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
        zoomTime(0.8);
    }
}

