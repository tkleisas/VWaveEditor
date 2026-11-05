#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace vwave::app;
using namespace vwave::ui;

void App::drawMenuBar() {
    m_menus.clear();
    const bool audio = hasTracks() && !activeDocument().audio().empty();
    const bool tracks = hasTracks();
    auto action = [](std::string label, std::function<void()> fn,
                     bool enabled = true, std::string shortcut = {}) {
        MenuItem item;
        item.label = std::move(label);
        item.action = std::move(fn);
        item.enabled = enabled;
        item.shortcut = std::move(shortcut);
        return item;
    };
    auto separator = [] {
        MenuItem item;
        item.separator = true;
        return item;
    };

    Menu file;
    file.title = "File";
    file.items = {
        action("Open...",
               [this] {
                   openDialog(DialogMode::Open, "Open audio, project, or image", "",
                              false);
               }, true, "Ctrl+O"),
        action("Add Track...",
               [this] {
                   openDialog(DialogMode::AddTrack, "Add audio track", "",
                              false);
               }),
        separator(),
        action("Save Mixdown...",
               [this] {
                   openDialog(DialogMode::SaveAudio, "Export mixdown",
                              suggestedMixdownPath(m_projectPath, m_audioPath),
                              true);
               }, tracks, "Ctrl+S"),
        action("Save Project...",
               [this] {
                   openDialog(DialogMode::SaveProject, "Save project",
                              m_projectPath, true);
               }, tracks, "Ctrl+Shift+S"),
        separator(),
        action("Quit", [this] { m_running = false; }, true, "Ctrl+Q")};
    Menu edit;
    edit.title = "Edit";
    edit.items = {
        action("Undo",
               [this] {
                   m_player.stop();
                   if (!m_timeline.undo()) setStatus("Nothing to undo");
                   else {
                       syncTrackControls();
                       markEdited();
                       setStatus("Undone");
                   }
               }, audio, "Ctrl+Z"),
        action("Redo",
               [this] {
                   m_player.stop();
                   if (!m_timeline.redo()) setStatus("Nothing to redo");
                   else {
                       syncTrackControls();
                       markEdited();
                       setStatus("Redone");
                   }
               }, audio, "Ctrl+Y"),
        separator(),
        action("Copy",
               [this] {
                   setStatus(activeDocument().copySelection()
                                 ? "Copied selection"
                                 : "Select audio first");
               }, audio, "Ctrl+C"),
        action("Cut",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().cutSelection(error);
                   }, "Cut selection");
               }, audio, "Ctrl+X"),
        action("Paste",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().paste(error);
                   }, "Pasted");
               }, audio, "Ctrl+V"),
        action("Delete",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().deleteSelection(error);
                   }, "Selection deleted");
               }, audio, "Del"),
        action("Crop",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().cropToSelection(error);
                   }, "Cropped to selection");
               }, audio),
        separator(),
        action("Select All",
               [this] {
                   m_timeline.setSelection(0, m_timeline.lengthFrames());
                   setStatus("Selected everything");
               }, tracks, "Ctrl+A"),
        action("Clear Selection",
               [this] {
                   m_timeline.clearSelection();
                   setStatus("Selection cleared");
               }, m_timeline.selection().active, "Esc"),
        action("Selection to New Track",
               [this] { copySelectionToNewTrack(); }, audio)};
    Menu track;
    track.title = "Track";
    track.items = {
        action("Add Track...",
               [this] {
                   openDialog(DialogMode::AddTrack, "Add audio track", "",
                              false);
               }),
        action("New Empty Track", [this] { addEmptyTrack(); }, true),
        action("Remove Active Track",
               [this] { removeTrack(m_timeline.activeTrack()); }, tracks),
        separator(),
        action("Mute Active",
               [this] {
                   const int index = m_timeline.activeTrack();
                   const bool muted = !m_timeline.track(index).muted;
                   m_timeline.setTrackMuted(index, muted);
                   updateMixer();
                   setStatus(muted ? "Track muted" : "Track unmuted");
               }, tracks),
        action("Solo Active",
               [this] {
                   const int index = m_timeline.activeTrack();
                   const bool solo = !m_timeline.track(index).solo;
                   m_timeline.setTrackSolo(index, solo);
                   updateMixer();
                   setStatus(solo ? "Track solo" : "Solo cleared");
               }, tracks),
        action("Arm Active",
               [this] {
                   const int index = m_timeline.activeTrack();
                   const bool armed = !m_timeline.track(index).armed;
                   for (int i = 0; i < m_timeline.trackCount(); ++i)
                       m_timeline.setTrackArmed(i, i == index && armed);
                   setStatus(armed ? "Track armed for recording"
                                   : "Track disarmed");
               }, tracks),
        separator(),
        action(m_recordingController.recording() ? "Stop Recording" : "Record",
               [this] { toggleRecording(); }, true, "F9")};
    Menu process;
    process.title = "Process";
    process.items = {
        action("Apply Gain",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().gainDb(m_gainDb.value, error);
                   }, "Gain applied");
               }, audio),
        action("Normalize",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().normalize(m_normalizePeak.value,
                                                         error);
                   }, "Normalized");
               }, audio),
        action("Reverse",
               [this] {
                   runEdit([this](std::string* error) {
                       return activeDocument().reverseSelection(error);
                   }, "Reversed");
               }, audio),
        action("Invert", [this] {
            runEdit([this](std::string* error) {
                return activeDocument().invertSelection(error);
            }, "Polarity inverted");
        }, audio),
        action("Fade In", [this] {
            runEdit([this](std::string* error) {
                return activeDocument().fadeIn(error);
            }, "Fade in applied");
        }, audio),
        action("Fade Out", [this] {
            runEdit([this](std::string* error) {
                return activeDocument().fadeOut(error);
            }, "Fade out applied");
        }, audio),
        separator(),
        action("Apply Stretch",
               [this] { stretchSelection(m_processTarget == 1); }, audio),
        action("Apply Pitch",
               [this] { pitchSelection(m_processTarget == 1); }, audio),
        separator(),
        action("Analyze Stems",
               [this] {
                   setTab(vwave::EditorTab::Mastering);
                   analyzeMasteringStems();
               }, tracks, "4"),
        action("Regenerate Active Track",
               [this] { regenerateActiveTrack(); },
               tracks && !m_timeline.track(m_timeline.activeTrack())
                             .recipe.empty()),
        action("Export Mastering Report...",
               [this] {
                   openDialog(DialogMode::SaveMasteringReport,
                              "Export mastering report",
                              (std::filesystem::current_path() /
                               "mastering.json").string(),
                              true);
               }, m_masteringController.valid())};
    Menu view;
    view.title = "View";
    view.items = {
        action("Waveform", [this] { setTab(vwave::EditorTab::Waveform); },
               true, "1"),
        action("Spectrum", [this] { setTab(vwave::EditorTab::Spectrum); },
               true, "2"),
        action("Wavetable", [this] { setTab(vwave::EditorTab::Wavetable); },
               true, "3"),
        action("Mastering", [this] { setTab(vwave::EditorTab::Mastering); },
               true, "4"),
        action("Image", [this] { setTab(vwave::EditorTab::Image); }, true,
               "5"),
        action("Scope", [this] { setTab(vwave::EditorTab::Oscilloscope); },
               true, "6"),
        separator(),
        action("Zoom In", [this] { zoomTime(1.25); }, tracks, "+"),
        action("Zoom Out", [this] { zoomTime(0.8); }, tracks, "-"),
        action("Fit Time", [this] { fitTime(); }, tracks),
        action("Fit Frequency", [this] { fitFrequency(); }, tracks)};
    Menu settings;
    settings.title = "Settings";
    settings.items = {
        action("Settings...", [this] { m_settingsController.open(); }, true, "Ctrl+,"),
        separator(),
        action("Dark Theme", [this] { m_settingsController.setTheme("dark"); },
               m_settingsController.settings().theme != "dark"),
        action("Light Theme", [this] { m_settingsController.setTheme("light"); },
               m_settingsController.settings().theme != "light")};
    m_menus = {file, edit, track, process, view, settings};

    SDL_SetRenderDrawColor(m_renderer, kMenu.r, kMenu.g, kMenu.b, 255);
    SDL_RenderFillRect(m_renderer, &m_menubarRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, m_menubarRect.y + m_menubarRect.h - 1,
                       m_menubarRect.w, m_menubarRect.y + m_menubarRect.h - 1);
    drawText("VWAVE", 14, 4, kAccent, fontBold());
    m_menuBarControls.begin(
        {m_menubarRect.x, m_menubarRect.y, m_menubarRect.w,
         m_menubarRect.h});
    int x = 78;
    for (int index = 0; index < static_cast<int>(m_menus.size()); ++index) {
        Menu& menu = m_menus[static_cast<size_t>(index)];
        const int width = textWidth(menu.title, fontNormal()) + 22;
        menu.titleRect = {x, 0, width, m_menubarRect.h};
        auto& button = m_menuBarControls.addButton(
            menu.title, {x, 0, width, m_menubarRect.h},
            [this, index] {
                m_openMenu = m_openMenu == index ? -1 : index;
            },
            false, index == m_openMenu, true);
        button.flat = true;
        button.font = FontRole::Normal;
        x += width + 2;
    }
    if (m_openMenu >= static_cast<int>(m_menus.size())) m_openMenu = -1;
    m_menuBarControls.end(m_uiPainter.get());
}

void App::drawMenuDropdown() {
    m_openMenuRect = {};
    if (m_openMenu < 0 || m_openMenu >= static_cast<int>(m_menus.size()))
        return;
    Menu& menu = m_menus[static_cast<size_t>(m_openMenu)];
    int width = 190;
    for (const auto& item : menu.items) {
        if (item.separator) continue;
        const int shortcutWidth = item.shortcut.empty()
                                      ? 0
                                      : textWidth(item.shortcut, fontSmall()) + 16;
        width = std::max(width, textWidth(item.label, fontNormal()) +
                                    shortcutWidth + 34);
    }
    int height = 8;
    for (const auto& item : menu.items)
        height += item.separator ? 7 : 24;
    height += 8;
    m_openMenuRect = {menu.titleRect.x, m_menubarRect.h, width, height};
    if (m_openMenuRect.x + m_openMenuRect.w > m_menubarRect.w)
        m_openMenuRect.x = m_menubarRect.w - m_openMenuRect.w;

    std::vector<PopupItem> items;
    items.reserve(menu.items.size());
    for (const auto& item : menu.items)
        items.push_back(
            {item.label, item.shortcut, item.action, item.enabled,
             item.separator});
    m_menuPopup.bounds = {m_openMenuRect.x, m_openMenuRect.y,
                          m_openMenuRect.w, m_openMenuRect.h};
    m_menuPopup.setItems(std::move(items));
    if (m_uiPainter) m_menuPopup.draw(*m_uiPainter);
}

void App::drawFooter() {
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_footerRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, m_footerRect.y, m_footerRect.w,
                       m_footerRect.y);
    const bool statusActive = SDL_GetTicks() < m_statusUntil;
    const std::string left = statusActive
                                 ? m_status
                                 : "Space play | Wheel zoom | Drag select | "
                                   "Alt+drag shift | 1-6 views | Ctrl+Z undo";
    drawText(truncateText(left, fontSmall(), m_footerRect.w - 380),
             m_footerRect.x + 14, m_footerRect.y + 7,
             statusActive ? kAccent : kTextFaint, fontSmall());
    if (m_recordingController.recording()) {
        std::ostringstream recording;
        const int64_t frames = m_recordingController.capturedFrames();
        recording << "REC " << formatSeconds(
            static_cast<double>(frames) /
            std::max(1, m_recordingController.capturedSampleRate()));
        const std::string text = recording.str();
        drawText(text, m_footerRect.w - textWidth(text, fontSmall()) - 14,
                 m_footerRect.y + 7, kRed, fontSmall());
    } else {
        const std::string right =
            "F9 record | B beats | T slice | L loop | Wheel zoom | 1/2/3 views";
        drawText(right, m_footerRect.w - textWidth(right, fontSmall()) - 14,
                 m_footerRect.y + 7, kTextFaint, fontSmall());
    }
}

void App::drawSettingsDialog() {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 170);
    SDL_Rect overlay{0, 0, m_menubarRect.w,
                     m_footerRect.y + m_footerRect.h};
    SDL_RenderFillRect(m_renderer, &overlay);
    SDL_Rect box{m_menubarRect.w / 2 - 380, m_footerRect.y / 2 - 190, 760, 380};
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &box);
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 255);
    SDL_RenderDrawRect(m_renderer, &box);
    drawText("Settings", box.x + 24, box.y + 18, kText, fontBold());
    drawText("Changes are saved immediately.", box.x + 24, box.y + 40,
             kTextFaint, fontSmall());

    m_settingsControls.begin(
        {box.x, box.y, box.w, box.h});
    const auto drawRow = [&](int row, const std::string& label,
                             const std::string& value,
                             std::function<void()> previousAction,
                             std::function<void()> nextAction) {
        const int y = box.y + 84 + row * 66;
        SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                               kPanelRaised.b, 255);
        SDL_Rect rowRect{box.x + 24, y, box.w - 48, 52};
        SDL_RenderFillRect(m_renderer, &rowRect);
        drawText(label, rowRect.x + 12, rowRect.y + 8, kTextDim, fontSmall());
        drawText(truncateText(value, fontNormal(), rowRect.w - 220),
                 rowRect.x + 12, rowRect.y + 26, kText, fontNormal());
        m_settingsControls.addButton(
            "<", {rowRect.x + rowRect.w - 96, rowRect.y + 14, 30, 24},
            std::move(previousAction));
        m_settingsControls.addButton(
            ">", {rowRect.x + rowRect.w - 56, rowRect.y + 14, 30, 24},
            std::move(nextAction));
    };

    const std::string output =
        m_settingsController.outputIndex() > 0 &&
                m_settingsController.outputIndex() < static_cast<int>(m_settingsController.outputDevices().size())
            ? m_settingsController.outputDevices()[static_cast<size_t>(m_settingsController.outputIndex())]
            : "System Default";
    const std::string input =
        m_settingsController.inputIndex() > 0 &&
                m_settingsController.inputIndex() < static_cast<int>(m_settingsController.inputDevices().size())
            ? m_settingsController.inputDevices()[static_cast<size_t>(m_settingsController.inputIndex())]
            : "System Default";
    drawRow(0, "Audio output", output,
            [this] { m_settingsController.cycleOutputDevice(-1); },
            [this] { m_settingsController.cycleOutputDevice(1); });
    drawRow(1, "Audio input", input,
            [this] { m_settingsController.cycleInputDevice(-1); },
            [this] { m_settingsController.cycleInputDevice(1); });
    drawRow(2, "Theme",
            m_settingsController.settings().theme == "light" ? "Light" : "Dark",
            [this] {
                m_settingsController.setTheme(
                    m_settingsController.settings().theme == "dark" ? "light"
                                                                     : "dark");
            },
            [this] {
                m_settingsController.setTheme(
                    m_settingsController.settings().theme == "dark" ? "light"
                                                                     : "dark");
            });

    m_settingsControls.addButton(
        "Close", {box.x + box.w - 118, box.y + box.h - 52, 94, 28},
        [this] { m_settingsController.close(); }, true);
    m_settingsControls.end(m_uiPainter.get());
    drawText(m_settingsController.path(), box.x + 24, box.y + box.h - 34, kTextFaint,
             fontSmall());
}
