#include "App.h"

#include "services/FileService.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <sstream>
#include <utility>

using namespace vwave::app;
using namespace vwave::ui;

void App::createToolbarWidgets() {
    m_toolbarRoot = std::make_unique<Widget>();
    m_toolbarRoot->bounds = {m_toolbarRect.x, m_toolbarRect.y,
                             m_toolbarRect.w, m_toolbarRect.h};
    m_toolbarButtons.clear();
    m_toolbarButtons.reserve(static_cast<size_t>(ToolbarButton::Count));

    auto add = [this](ToolbarButton id, const std::string& label, int x,
                      int width, std::function<void()> action,
                      bool accent = false) {
        auto button = std::make_unique<PushButton>();
        button->label = label;
        button->bounds = {x, m_toolbarRect.y + 13, width, 29};
        button->accent = accent;
        button->onClick = std::move(action);
        auto* pointer = button.get();
        m_toolbarRoot->addChild(std::move(button));
        if (m_toolbarButtons.size() != static_cast<size_t>(id))
            m_toolbarButtons.resize(static_cast<size_t>(id));
        m_toolbarButtons.push_back(pointer);
    };

    add(ToolbarButton::Waveform, "Waveform", 154, 96,
        [this] { setTab(vwave::EditorTab::Waveform); });
    add(ToolbarButton::Spectrum, "Spectrum", 254, 96,
        [this] { setTab(vwave::EditorTab::Spectrum); });
    add(ToolbarButton::Wavetable, "Wavetable", 354, 96,
        [this] { setTab(vwave::EditorTab::Wavetable); });
    add(ToolbarButton::Mastering, "Mastering", 454, 96,
        [this] { setTab(vwave::EditorTab::Mastering); });
    add(ToolbarButton::Image, "Image", 554, 76,
        [this] { setTab(vwave::EditorTab::Image); });
    add(ToolbarButton::Scope, "Scope", 634, 76,
        [this] { setTab(vwave::EditorTab::Oscilloscope); });
    add(ToolbarButton::Open, "Open", 720, 60,
        [this] {
            openDialog(DialogMode::Open, "Open audio, project, or image", "",
                       false);
        });
    add(ToolbarButton::AddTrack, "Add Track", 786, 76,
        [this] {
            openDialog(DialogMode::AddTrack, "Add audio track", "", false);
        });
    add(ToolbarButton::SaveWav, "Save WAV", 868, 78,
        [this] {
            openDialog(DialogMode::SaveAudio, "Export mixdown",
                       suggestedMixdownPath(m_projectPath, m_audioPath), true);
        });
    add(ToolbarButton::Project, "Project", 952, 66,
        [this] {
            openDialog(DialogMode::SaveProject, "Save project", m_projectPath,
                       true);
        });
    add(ToolbarButton::Play, "Play", 1024, 64,
        [this] { togglePlay(); }, true);
    add(ToolbarButton::Stop, "Stop", 1094, 54,
        [this] { stopPlayback(); });
    add(ToolbarButton::ZoomOut, "-", 1158, 26,
        [this] { zoomTime(0.8); });
    add(ToolbarButton::ZoomIn, "+", 1188, 26,
        [this] { zoomTime(1.25); });
    add(ToolbarButton::Fit, "Fit", 1218, 40,
        [this] { fitTime(); });
    add(ToolbarButton::Start, "|<", 1264, 30,
        [this] { goToStart(); });
    add(ToolbarButton::End, ">|", 1298, 30,
        [this] { goToEnd(); });
    add(ToolbarButton::Time, "0:00", 1332, 100,
        [this] {
            openDialog(
                DialogMode::SeekTime, "Go to time",
                formatTimeInput(static_cast<double>(m_playhead) /
                                std::max(1, m_timeline.sampleRate())),
                false);
        });

    m_toolbarHost.setRoot(m_toolbarRoot.get());
}

void App::syncToolbarWidgets() {
    if (m_toolbarButtons.size() != static_cast<size_t>(ToolbarButton::Count)) return;
    auto& buttons = m_toolbarButtons;
    auto setActive = [&](ToolbarButton id, bool active) {
        buttons[static_cast<size_t>(id)]->active = active;
    };
    setActive(ToolbarButton::Waveform,
              m_tab == vwave::EditorTab::Waveform);
    setActive(ToolbarButton::Spectrum,
              m_tab == vwave::EditorTab::Spectrum);
    setActive(ToolbarButton::Wavetable,
              m_tab == vwave::EditorTab::Wavetable);
    setActive(ToolbarButton::Mastering,
              m_tab == vwave::EditorTab::Mastering);
    setActive(ToolbarButton::Image, m_tab == vwave::EditorTab::Image);
    setActive(ToolbarButton::Scope,
              m_tab == vwave::EditorTab::Oscilloscope);

    const bool playing = m_player.isPlaying();
    const bool hasAudio = !m_timeline.empty();
    auto& play = *buttons[static_cast<size_t>(ToolbarButton::Play)];
    play.label = playing ? "Playing" : "Play";
    play.active = playing;
    play.enabled = hasAudio;
    buttons[static_cast<size_t>(ToolbarButton::Stop)]->enabled = playing;
    buttons[static_cast<size_t>(ToolbarButton::ZoomOut)]->enabled = hasAudio;
    buttons[static_cast<size_t>(ToolbarButton::ZoomIn)]->enabled = hasAudio;
    buttons[static_cast<size_t>(ToolbarButton::Fit)]->enabled = hasAudio;
    buttons[static_cast<size_t>(ToolbarButton::Start)]->enabled = hasAudio;
    buttons[static_cast<size_t>(ToolbarButton::End)]->enabled = hasAudio;

    std::ostringstream timeLabel;
    timeLabel << formatSeconds(static_cast<double>(m_playhead) /
                               std::max(1, m_timeline.sampleRate()));
    auto& time = *buttons[static_cast<size_t>(ToolbarButton::Time)];
    time.label = timeLabel.str();
    time.enabled = hasAudio;
}

void App::drawToolbar() {
    if (!m_toolbarRoot) createToolbarWidgets();
    if (!m_uiPainter) return;
    m_toolbarRoot->bounds = {m_toolbarRect.x, m_toolbarRect.y,
                             m_toolbarRect.w, m_toolbarRect.h};
    syncToolbarWidgets();

    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_toolbarRect);
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 255);
    SDL_Rect accentLine{0, m_toolbarRect.y + m_toolbarRect.h - 2,
                        m_toolbarRect.w, 2};
    SDL_RenderFillRect(m_renderer, &accentLine);
    m_toolbarRoot->draw(*m_uiPainter);
}
