#include "App.h"

#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"
#include "vwave/Processing.h"
#include "vwave/Recipe.h"
#include "vwave/Synth.h"
#include "vwave/Version.h"

#include "ui/UiConstants.h"
#include "services/FileService.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

using namespace vwave::app;
using namespace vwave::ui;

App::App()
    : m_analysis(m_jobs, m_dispatcher),
      m_fileLoader(
          m_jobs, m_dispatcher,
          [this](const std::string& status) { setStatus(status); }),
      m_fileSaver(
          m_jobs, m_dispatcher,
          [this](const std::string& status) { setStatus(status); }),
      m_prefetch(m_jobs, m_dispatcher),
      m_analysisData(
          m_analysis, [this](const std::string& status) { setStatus(status); }),
      m_stereo(
          m_timeline, m_player,
          [this] {
              m_clips.reset();
              syncTrackControls();
              clearAnalysis();
              m_view.fit(m_timeline.lengthFrames());
              m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
          },
          [this](const std::string& status) { setStatus(status); }),
      m_tracks(
          m_timeline, m_player, m_playhead,
          [this] {
              m_clips.onTrackChanged(activeDocument().hasClips());
              clearAnalysis();
              m_wavetableController.clear();
              m_view.clamp(m_timeline.lengthFrames());
          },
          [this] {
              m_clips.reset();
              clearAnalysis();
              m_wavetableController.clear();
              m_view.fit(m_timeline.lengthFrames());
              m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
              m_loop.clamp(m_timeline.lengthFrames());
              m_audioPath =
                  hasTracks() ? activeDocument().sourcePath() : std::string{};
              if (m_timeline.empty()) m_projectPath.clear();
              markDirty();
          },
          [this](const std::string& status) { setStatus(status); }),
      m_playback(
          m_timeline, m_player, m_playhead, m_loop,
          [this](const std::string& status) { setStatus(status); },
          [this](int64_t playhead) {
              const double viewEnd = m_view.startFrame + m_view.visibleFrames;
              if (playhead < m_view.startFrame || playhead > viewEnd) {
                  m_view.startFrame =
                      static_cast<double>(playhead) -
                      m_view.visibleFrames * 0.15;
                  m_view.clamp(m_timeline.lengthFrames());
              }
          }),
      m_recordingController(
          m_timeline, m_player, m_playhead,
          [this](const std::string& status) { setStatus(status); },
          [this] {
              m_clips.onTrackChanged(activeDocument().hasClips());
              syncTrackControls();
              clearAnalysis();
          },
          [this] {
              syncTrackControls();
              clearAnalysis();
              m_view.fit(m_timeline.lengthFrames());
              m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
              updateWindowTitle();
          }),
      m_clips(
          m_timeline, m_playhead, m_stretchRatio,
          [this](const std::function<bool(std::string*)>& edit,
                 const std::string& success) { runEdit(edit, success); },
          [this](const std::string& status) { setStatus(status); }),
      m_selection(
          m_timeline, m_player, m_playhead, m_stretchRatio,
          [this](const std::function<bool(std::string*)>& edit,
                 const std::string& success) { runEdit(edit, success); },
          [this](bool documentEdited) {
              if (documentEdited)
                  markEdited();
              else
                  clearAnalysis();
              m_view.clamp(m_timeline.lengthFrames());
              m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
          },
          [this](const std::string& status) { setStatus(status); }),
      m_spectralController(
          m_timeline, m_player,
          [this](const std::function<bool(std::string*)>& edit,
                 const std::string& success) { runEdit(edit, success); },
          [this](const std::string& status) { setStatus(status); }),
      m_masteringController(
          m_timeline, m_player, m_jobs, m_dispatcher,
          [this](const std::string& status) { setStatus(status); },
          [this] {
              syncTrackControls();
              m_view.fit(m_timeline.lengthFrames());
              clearAnalysis();
              setTab(vwave::EditorTab::Waveform);
              updateWindowTitle();
          }),
      m_imageController(
          m_renderer, m_jobs, m_dispatcher, m_timeline, m_player,
          [this](const std::string& status) { setStatus(status); },
          [this](int64_t start) {
              syncTrackControls();
              m_view.fit(m_timeline.lengthFrames());
              m_playhead = start;
              clearAnalysis();
              setTab(vwave::EditorTab::Waveform);
              updateWindowTitle();
          },
          [this] { setTab(vwave::EditorTab::Image); }),
      m_scopeController(
          m_jobs, m_dispatcher, m_timeline, m_player,
          [this](const std::string& status) { setStatus(status); },
          [this](int64_t start) {
              syncTrackControls();
              m_view.fit(m_timeline.lengthFrames());
              m_playhead = start;
              clearAnalysis();
              setTab(vwave::EditorTab::Waveform);
              updateWindowTitle();
          }),
      m_wavetableController(
          m_jobs, m_dispatcher, m_timeline, m_player,
          [this](const std::string& status) { setStatus(status); },
          [this] {
              syncTrackControls();
              m_view.fit(m_timeline.lengthFrames());
              m_playhead = 0;
              clearAnalysis();
              setTab(vwave::EditorTab::Waveform);
              updateWindowTitle();
          },
          [this] {
              syncWavetableDesignControls();
              setTab(vwave::EditorTab::Wavetable);
          }),
      m_settingsController(
          m_player, [this](const std::string& status) { setStatus(status); },
          [this](const std::string& theme) {
              applyPalette(theme);
              if (m_uiPainter) m_uiPainter->clearTextCache();
          }) {
    m_menuHost.setRoot(&m_menuPopup);
    m_contextMenuHost.setRoot(&m_contextMenuPopup);
    m_dialog.setConfirmHandler(
        [this](DialogMode mode, const std::string& path) {
            applyDialog(mode, path);
        });
    m_gainDb = {-24.0, 24.0, 0.0, false, 0.1};
    m_normalizePeak = {-24.0, 0.0, -1.0, false, 0.1};
    m_stretchRatio = {0.25, 8.0, 2.0, true, 0.0};
    m_pitchSemitones = {-24.0, 24.0, 12.0, false, 1.0};
    m_beatSensitivity = {0.0, 1.0, 0.5, false, 0.05};
    m_sliceSensitivity = {0.0, 1.0, 0.5, false, 0.05};
    m_minSliceMs = {5.0, 500.0, 30.0, false, 1.0};
    m_beatsPerSlice = {1.0, 8.0, 1.0, false, 1.0};
    m_spectralLow = {20.0, 20000.0, 200.0, true, 0.0};
    m_spectralHigh = {20.0, 20000.0, 4000.0, true, 0.0};
    m_spectralGain = {-60.0, 24.0, 6.0, false, 0.5};
    m_spectralGate = {-96.0, -6.0, -40.0, false, 1.0};
    m_spectralCutoff = {20.0, 20000.0, 1000.0, true, 0.0};
    m_spectralBlurTime = {0.0, 8.0, 1.0, false, 1.0};
    m_spectralBlurFrequency = {0.0, 32.0, 2.0, false, 1.0};
    m_wavetableFrameCount = {1.0, 64.0, 8.0, false, 1.0};
    m_synthNote = {0.0, 127.0, 60.0, false, 1.0};
    m_synthRoot = {0.0, 127.0, 60.0, false, 1.0};
    m_synthDuration = {0.05, 10.0, 2.0, true, 0.0};
    m_synthMorph = {0.0, 1.0, 0.0, false, 0.01};
    m_synthUnison = {1.0, 8.0, 1.0, false, 1.0};
    m_synthGain = {0.0, 1.0, 0.7, false, 0.01};
    m_imageDuration = {0.1, 60.0, 4.0, true, 0.1};
    m_imageMinHz = {20.0, 20000.0, 50.0, true, 1.0};
    m_imageMaxHz = {100.0, 20000.0, 12000.0, true, 1.0};
    m_imageGamma = {0.2, 4.0, 1.0, false, 0.01};
    m_imageContrast = {0.2, 4.0, 1.0, false, 0.01};
    m_imageThreshold = {0.0, 0.95, 0.0, false, 0.01};
    m_imageLoops = {1.0, 8.0, 1.0, false, 1.0};
    m_imageIterations = {0.0, 64.0, 24.0, false, 1.0};
    m_scopePointCount = {16.0, 1024.0, 256.0, true, 1.0};
    m_scopeSize = {0.1, 1.0, 0.8, false, 0.01};
    m_scopeA = {1.0, 9.0, 3.0, false, 1.0};
    m_scopeB = {1.0, 9.0, 2.0, false, 1.0};
    m_scopePhase = {0.0, 6.283185307179586, 0.0, false, 0.01};
    m_scopeTurns = {1.0, 16.0, 4.0, false, 1.0};
    m_scopeScanHz = {20.0, 2000.0, 120.0, true, 1.0};
    m_scopeDuration = {0.1, 30.0, 2.0, true, 0.1};
    m_scopeGain = {0.0, 1.0, 0.8, false, 0.01};
    m_scopeSmoothing = {0.0, 0.95, 0.0, false, 0.01};
    m_scopeKeyTime = {0.0, 30.0, 0.0, false, 0.01};
    m_scopeTranslateX = {-2.0, 2.0, 0.0, false, 0.01};
    m_scopeTranslateY = {-2.0, 2.0, 0.0, false, 0.01};
    m_scopeRotation = {-180.0, 180.0, 0.0, false, 1.0};
    m_scopeScale = {0.05, 4.0, 1.0, false, 0.01};
    m_scopeXYWindowMs = {1.0, 500.0, 25.0, false, 1.0};
    m_masteringBands = {8.0, 64.0, 32.0, true, 1.0};
    m_masteringMaxCut = {0.0, 24.0, 12.0, false, 0.5};
    m_masteringMaxBoost = {0.0, 6.0, 2.0, false, 0.5};
    m_masteringSmoothing = {0.0, 4.0, 1.0, false, 1.0};
    m_masteringCeiling = {-12.0, 0.0, -1.0, false, 0.1};
    m_framePositionControl = {0.0, 1.0, 0.0, false, 0.001};
    m_frameWeightControl = {0.0, 2.0, 1.0, false, 0.01};
    m_tableRootControl = {0.0, 127.0, 60.0, false, 1.0};
    m_harmonicCount = {1.0, 128.0, 16.0, true, 1.0};
    m_harmonicLimit = {1.0, 512.0, 64.0, true, 1.0};
    m_quantizeBits = {1.0, 24.0, 8.0, false, 1.0};
    generateScopePath();
}

App::~App() {
    m_player.close();
    m_uiPainter.reset();
    m_cursorController.clear();
    if (m_spectrogramTexture) SDL_DestroyTexture(m_spectrogramTexture);
    if (m_scopeXYTexture) SDL_DestroyTexture(m_scopeXYTexture);
    m_imageController.close();
    TTF_Font* fonts[] = {m_fontSmall, m_font, m_fontBold, m_fontTitle};
    for (int i = 0; i < 4; ++i) {
        if (!fonts[i]) continue;
        bool duplicate = false;
        for (int j = 0; j < i; ++j) {
            if (fonts[j] == fonts[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) TTF_CloseFont(fonts[i]);
    }
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    TTF_Quit();
    SDL_Quit();
}

bool App::init(const std::string& initialPath) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
        return false;
    if (TTF_Init() != 0) return false;
    m_window = SDL_CreateWindow("VWaveEditor", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, 1440, 900,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!m_window) return false;
    SDL_SetWindowMinimumSize(m_window, 1060, 760);
    m_renderer = SDL_CreateRenderer(
        m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer)
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    if (!m_renderer) return false;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    std::vector<std::string> regularCandidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    std::vector<std::string> boldCandidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
#ifdef _WIN32
    if (const char* windir = std::getenv("WINDIR")) {
        const std::filesystem::path fonts = std::filesystem::path(windir) / "Fonts";
        regularCandidates.push_back((fonts / "segoeui.ttf").string());
        regularCandidates.push_back((fonts / "arial.ttf").string());
        regularCandidates.push_back((fonts / "calibri.ttf").string());
        regularCandidates.push_back((fonts / "tahoma.ttf").string());
        boldCandidates.push_back((fonts / "segoeuib.ttf").string());
        boldCandidates.push_back((fonts / "arialbd.ttf").string());
        boldCandidates.push_back((fonts / "calibrib.ttf").string());
        boldCandidates.push_back((fonts / "tahomabd.ttf").string());
    }
#endif
    if (char* basePath = SDL_GetBasePath()) {
        regularCandidates.push_back(
            (std::filesystem::path(basePath) / "DejaVuSans.ttf").string());
        boldCandidates.push_back(
            (std::filesystem::path(basePath) / "DejaVuSans-Bold.ttf").string());
        SDL_free(basePath);
    }
    std::string regular = "DejaVuSans.ttf";
    std::string bold;
    for (const auto& candidate : regularCandidates) {
        if (std::filesystem::exists(candidate)) {
            regular = candidate;
            break;
        }
    }
    for (const auto& candidate : boldCandidates) {
        if (std::filesystem::exists(candidate)) {
            bold = candidate;
            break;
        }
    }
    if (bold.empty()) bold = regular;
    m_fontSmall = TTF_OpenFont(regular.c_str(), 12);
    m_font = TTF_OpenFont(regular.c_str(), 14);
    m_fontBold = TTF_OpenFont(bold.c_str(), 15);
    m_fontTitle = TTF_OpenFont(bold.c_str(), 23);
    if (!m_fontSmall) m_fontSmall = m_font;
    if (!m_fontBold) m_fontBold = m_font;
    if (!m_fontTitle) m_fontTitle = m_fontBold;
    m_ui.setRenderer(m_renderer);
    m_ui.setFonts(m_fontSmall, m_font, m_fontBold, m_fontTitle);
    m_uiPainter = std::make_unique<vwave::ui::SdlPainter>(
        m_renderer, m_fontSmall, m_font, m_fontBold, m_fontTitle);

    m_settingsController.load();
    m_player.setOutputDevice(
        m_settingsController.settings().outputDevice);
    if (!m_player.open() &&
        !m_settingsController.settings().outputDevice.empty()) {
        m_settingsController.clearOutputDevice();
        m_player.open();
    }
    m_view.fit(1);
    updateWindowTitle();
    setStatus("Drop audio/image or press Ctrl+O", 6000);
    if (!initialPath.empty()) loadFile(initialPath);
    return true;
}

int App::run() {
    const bool smokeTest = std::getenv("VWAVE_SMOKE_TEST") != nullptr;
    if (smokeTest) {
        vwave::Audio audio(2, 9600, 48000);
        for (int64_t frame = 0; frame < audio.numFrames(); ++frame) {
            const double value =
                0.2 * std::sin(2.0 * 3.141592653589793 * 440.0 *
                               static_cast<double>(frame) / 48000.0);
            audio.sample(0, frame) = static_cast<float>(value);
            audio.sample(1, frame) = static_cast<float>(value * 0.8);
        }
        const auto path =
            std::filesystem::temp_directory_path() /
            "vwave_editor_smoke.wav";
        std::string error;
        if (!vwave::writeAudio(path.string(), audio, {}, &error)) return 1;
        if (!loadAudio(path.string())) return 1;
        const Uint32 deadline = SDL_GetTicks() + 5000;
        while (!hasTracks() && SDL_GetTicks() < deadline) {
            m_dispatcher.drain();
            SDL_Delay(2);
        }
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        if (!hasTracks()) return 1;
        const int baseTracks = m_timeline.trackCount();
        if (!m_timeline.addEmptyTrack("Smoke undo", 0, &error)) return 1;
        if (!m_timeline.undo() ||
            m_timeline.trackCount() != baseTracks)
            return 1;
        if (!m_timeline.redo() ||
            m_timeline.trackCount() != baseTracks + 1)
            return 1;
        if (!m_timeline.undo()) return 1;
        layout();
        createToolbarWidgets();
        SDL_Event buttonDown{};
        buttonDown.type = SDL_MOUSEBUTTONDOWN;
        buttonDown.button.x = 254 + 48;
        buttonDown.button.y = m_toolbarRect.y + 13 + 14;
        buttonDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(buttonDown);
        SDL_Event buttonUp = buttonDown;
        buttonUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(buttonUp);
        if (m_tab != vwave::EditorTab::Spectrum) return 1;
        setTab(vwave::EditorTab::Waveform);
        drawSidebar();
        vwave::ui::PushButton* shiftButton = nullptr;
        for (auto* button : m_sidebarButtons) {
            if (button->visible && button->label == "Shift +1 s") {
                shiftButton = button;
                break;
            }
        }
        if (!shiftButton) return 1;
        const int64_t beforeShift = m_timeline.track(0).startFrame;
        SDL_Event sidebarDown{};
        sidebarDown.type = SDL_MOUSEBUTTONDOWN;
        sidebarDown.button.x = shiftButton->bounds.x + shiftButton->bounds.w / 2;
        sidebarDown.button.y = shiftButton->bounds.y + shiftButton->bounds.h / 2;
        sidebarDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(sidebarDown);
        SDL_Event sidebarUp = sidebarDown;
        sidebarUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(sidebarUp);
        if (m_timeline.track(0).startFrame == beforeShift) return 1;
        setTab(vwave::EditorTab::Spectrum);
        drawSidebar();
        vwave::ui::SidebarSlider* lowSlider = nullptr;
        for (auto* control : m_sidebarSliders) {
            if (control->visible && control->label == "Low frequency") {
                lowSlider = control;
                break;
            }
        }
        if (!lowSlider) return 1;
        const double beforeLow = m_spectralLow.value;
        SDL_Event sliderDown{};
        sliderDown.type = SDL_MOUSEBUTTONDOWN;
        sliderDown.button.x = lowSlider->bounds.x +
                              lowSlider->bounds.w * 3 / 4;
        sliderDown.button.y = lowSlider->bounds.y + 20;
        sliderDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(sliderDown);
        SDL_Event sliderUp = sliderDown;
        sliderUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(sliderUp);
        if (m_spectralLow.value == beforeLow) return 1;
        setTab(vwave::EditorTab::Waveform);
        drawWaveform(m_contentRect);
        vwave::ui::PushButton* muteButton = nullptr;
        for (auto* button : m_trackHeaderControls.buttons()) {
            if (button->visible && button->label == "M") {
                muteButton = button;
                break;
            }
        }
        if (!muteButton) return 1;
        const bool beforeMute = m_timeline.track(0).muted;
        SDL_Event muteDown{};
        muteDown.type = SDL_MOUSEBUTTONDOWN;
        muteDown.button.x = muteButton->bounds.x + muteButton->bounds.w / 2;
        muteDown.button.y = muteButton->bounds.y + muteButton->bounds.h / 2;
        muteDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(muteDown);
        SDL_Event muteUp = muteDown;
        muteUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(muteUp);
        if (m_timeline.track(0).muted == beforeMute) return 1;
        if (!m_timeline.undo()) return 1;
        if (m_timeline.track(0).muted != beforeMute) return 1;
        const auto* contextRow = m_timelineCanvas.rowForTrack(0);
        if (!contextRow) return 1;
        SDL_Event contextDown{};
        contextDown.type = SDL_MOUSEBUTTONDOWN;
        contextDown.button.x = contextRow->lane.x + contextRow->lane.w / 2;
        contextDown.button.y = contextRow->lane.y + contextRow->lane.h / 2;
        contextDown.button.button = SDL_BUTTON_RIGHT;
        handleEvent(contextDown);
        if (!m_contextMenuOpen) return 1;
        drawContextMenu();
        handleEvent(contextDown);
        if (m_contextMenuOpen) return 1;
        m_settingsController.open();
        drawSettingsDialog();
        vwave::ui::PushButton* closeSettingsButton = nullptr;
        for (auto* button : m_settingsControls.buttons()) {
            if (button->visible && button->label == "Close") {
                closeSettingsButton = button;
                break;
            }
        }
        if (!closeSettingsButton) return 1;
        SDL_Event closeDown{};
        closeDown.type = SDL_MOUSEBUTTONDOWN;
        closeDown.button.x =
            closeSettingsButton->bounds.x + closeSettingsButton->bounds.w / 2;
        closeDown.button.y =
            closeSettingsButton->bounds.y + closeSettingsButton->bounds.h / 2;
        closeDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(closeDown);
        SDL_Event closeUp = closeDown;
        closeUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(closeUp);
        if (m_settingsController.isOpen()) return 1;
        m_dialog.open(DialogMode::Open, "Open", "", false);
        m_dialog.draw(m_renderer, *m_uiPainter, m_toolbarRect.w,
                      m_footerRect.y + m_footerRect.h);
        vwave::ui::PushButton* cancelDialogButton = nullptr;
        for (auto* button : m_dialog.buttons()) {
            if (button->visible && button->label == "Cancel") {
                cancelDialogButton = button;
                break;
            }
        }
        if (!cancelDialogButton) return 1;
        SDL_Event dialogDown{};
        dialogDown.type = SDL_MOUSEBUTTONDOWN;
        dialogDown.button.x =
            cancelDialogButton->bounds.x + cancelDialogButton->bounds.w / 2;
        dialogDown.button.y =
            cancelDialogButton->bounds.y + cancelDialogButton->bounds.h / 2;
        dialogDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(dialogDown);
        SDL_Event dialogUp = dialogDown;
        dialogUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(dialogUp);
        if (m_dialog.isOpen()) return 1;
        drawMenuBar();
        vwave::ui::PushButton* viewMenuButton = nullptr;
        for (auto* button : m_menuBarControls.buttons()) {
            if (button->visible && button->label == "View") {
                viewMenuButton = button;
                break;
            }
        }
        if (!viewMenuButton) return 1;
        SDL_Event menuDown{};
        menuDown.type = SDL_MOUSEBUTTONDOWN;
        menuDown.button.x =
            viewMenuButton->bounds.x + viewMenuButton->bounds.w / 2;
        menuDown.button.y =
            viewMenuButton->bounds.y + viewMenuButton->bounds.h / 2;
        menuDown.button.button = SDL_BUTTON_LEFT;
        handleEvent(menuDown);
        SDL_Event menuUp = menuDown;
        menuUp.type = SDL_MOUSEBUTTONUP;
        handleEvent(menuUp);
        if (m_openMenu < 0) return 1;
        drawMenuDropdown();
        handleEvent(menuDown);
        handleEvent(menuUp);
        if (m_openMenu >= 0) return 1;
        auto waitForFile = [this](const std::filesystem::path& target) {
            const Uint32 deadline = SDL_GetTicks() + 5000;
            while (SDL_GetTicks() < deadline) {
                std::error_code sizeError;
                const auto size = std::filesystem::file_size(target, sizeError);
                if (!sizeError && size > 0) return true;
                m_dispatcher.drain();
                SDL_Delay(2);
            }
            return false;
        };
        const auto savePath =
            std::filesystem::temp_directory_path() /
            "vwave_editor_smoke_saved.wav";
        std::filesystem::remove(savePath, removeError);
        saveAudio(savePath.string());
        const bool audioSaved = waitForFile(savePath);
        std::filesystem::remove(savePath, removeError);
        if (!audioSaved) return 1;
        const auto projectPath =
            std::filesystem::temp_directory_path() /
            "vwave_editor_smoke_saved.vwdoc";
        std::filesystem::remove(projectPath, removeError);
        std::filesystem::remove_all(
            projectPath.string() + ".blocks", removeError);
        saveProject(projectPath.string());
        const bool projectSaved = waitForFile(projectPath);
        if (!projectSaved) return 1;
        const Uint32 pathDeadline = SDL_GetTicks() + 5000;
        while (m_projectPath.empty() && SDL_GetTicks() < pathDeadline) {
            m_dispatcher.drain();
            SDL_Delay(2);
        }
        if (m_projectPath.empty()) return 1;
        const auto recoveryPath =
            std::filesystem::path(projectPath.string() + ".autosave");
        std::filesystem::remove(recoveryPath, removeError);
        std::filesystem::remove_all(
            recoveryPath.string() + ".blocks", removeError);
        if (!m_timeline.track(0).document.addMarker(10, "recovery"))
            return 1;
        markEdited();
        m_lastEditTicks = SDL_GetTicks() - 3000;
        updateAutosave();
        const bool recoverySaved = waitForFile(recoveryPath);
        if (!recoverySaved) return 1;
        m_timeline.track(0).document.clearMarkers();
        recoverAutosave();
        const Uint32 recoveryDeadline = SDL_GetTicks() + 5000;
        while (m_timeline.track(0).document.markers().empty() &&
               SDL_GetTicks() < recoveryDeadline) {
            m_dispatcher.drain();
            SDL_Delay(2);
        }
        const bool recovered =
            !m_timeline.track(0).document.markers().empty() &&
            m_timeline.track(0).document.markers()[0].name == "recovery";
        std::filesystem::remove_all(
            recoveryPath.string() + ".blocks", removeError);
        std::filesystem::remove(recoveryPath, removeError);
        std::filesystem::remove(projectPath, removeError);
        std::filesystem::remove_all(
            projectPath.string() + ".blocks", removeError);
        if (!recovered) return 1;
    }
    static const vwave::EditorTab kSmokeTabs[] = {
        vwave::EditorTab::Waveform, vwave::EditorTab::Spectrum,
        vwave::EditorTab::Wavetable, vwave::EditorTab::Mastering,
        vwave::EditorTab::Image, vwave::EditorTab::Oscilloscope};
    int frames = 0;
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) handleEvent(event);
        updatePlayback();
        updateRecording();
        m_dispatcher.drain();
        updateAutosave();
        if (smokeTest) {
            constexpr int kTabCount =
                static_cast<int>(sizeof(kSmokeTabs) / sizeof(kSmokeTabs[0]));
            if (frames < 18) {
                setTab(kSmokeTabs[frames / 3 % kTabCount]);
            } else if (frames == 18) {
                m_settingsController.open();
            } else if (frames == 22) {
                m_settingsController.close();
            }
        }
        draw();
        ++frames;
        if (smokeTest && frames >= 26) break;
        SDL_Delay(4);
    }
    return 0;
}

void App::layout() {
    int width = 1440;
    int height = 900;
    SDL_GetWindowSize(m_window, &width, &height);
    m_menubarRect = {0, 0, width, 28};
    m_toolbarRect = {0, 28, width, 56};
    m_footerRect = {0, height - 28, width, 28};
    m_sidebarRect = {0, 84, 292, height - 112};
    m_contentRect = {292, 84, width - 292, height - 112};
}

void App::setStatus(const std::string& text, Uint32 milliseconds) {
    m_status = text;
    m_statusUntil = SDL_GetTicks() + milliseconds;
}

void App::updateWindowTitle() {
    if (!m_window) return;
    std::string name = m_projectPath.empty() ? m_audioPath : m_projectPath;
    if (name.empty()) name = "Untitled";
    const std::string title =
        std::string("VWaveEditor " VWAVE_VERSION " - ") +
        (m_modified ? "*" : "") + baseName(name);
    SDL_SetWindowTitle(m_window, title.c_str());
}

void App::draw() {
    layout();
    SDL_SetRenderDrawColor(m_renderer, kBackground.r, kBackground.g,
                           kBackground.b, kBackground.a);
    SDL_RenderClear(m_renderer);

    drawMenuBar();
    drawToolbar();
    drawSidebar();

    SDL_Rect content = m_contentRect;
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b,
                           kContent.a);
    SDL_RenderFillRect(m_renderer, &content);

    if (m_timeline.empty() && m_tab != vwave::EditorTab::Image &&
        m_tab != vwave::EditorTab::Mastering &&
        m_tab != vwave::EditorTab::Oscilloscope) {
        drawText("Open audio/image or create your first track", content.x + 40,
                 content.y + 60, kTextDim, fontTitle());
        drawText("Ctrl+O or drag and drop WAV, AIFF, FLAC, OGG",
                 content.x + 40, content.y + 96, kTextFaint, fontNormal());
        drawText("Add more tracks with Add Track. Every edit is undoable.",
                 content.x + 40, content.y + 118, kTextFaint, fontSmall());
    } else if (m_tab == vwave::EditorTab::Waveform) {
        drawWaveform(content);
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        drawSpectrum(content);
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        drawWavetable(content);
    } else if (m_tab == vwave::EditorTab::Mastering) {
        drawMastering(content);
    } else if (m_tab == vwave::EditorTab::Image) {
        drawImage(content);
    } else {
        drawOscilloscope(content);
    }

    drawFooter();
    drawMenuDropdown();
    drawContextMenu();
    if (m_settingsController.isOpen()) drawSettingsDialog();
    if (m_dialog.isOpen() && m_uiPainter)
        m_dialog.draw(m_renderer, *m_uiPainter, m_toolbarRect.w,
                      m_footerRect.y + m_footerRect.h);
    SDL_RenderPresent(m_renderer);
}

void App::addSlider(const std::string& label, const std::string& valueText,
                    int x, int y, int w, vwave::ParameterSlider& slider,
                    std::function<void(double)> onChanged) {
    if (m_buildingSidebar) {
        addSidebarSlider(label, valueText, x, y, w, slider,
                         std::move(onChanged));
        return;
    }
    if (m_buildingTrackHeader) {
        m_trackHeaderControls.addSlider(label, valueText, {x, y, w, 26},
                                        slider, std::move(onChanged));
        return;
    }
}

vwave::Document& App::activeDocument() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return m_emptyDocument;
    return m_timeline.track(index).document;
}

const vwave::Document& App::activeDocument() const {
    const int index = m_timeline.activeTrack();
    if (index < 0) return m_emptyDocument;
    return m_timeline.track(index).document;
}

void App::zoomTime(double factor) {
    const int width = m_tab == vwave::EditorTab::Spectrum
                          ? std::max(1, m_spectrumArea.w)
                          : std::max(1, m_waveArea.w);
    m_view.zoomAt(factor, width * 0.5, width, m_timeline.lengthFrames());
}

void App::fitTime() {
    m_view.fit(m_timeline.lengthFrames());
}

void App::zoomFrequency(double factor) {
    const double maxHz = std::min(
        20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
    const double center =
        std::sqrt(m_spectralView.minHz * m_spectralView.maxHz);
    m_spectralView.zoom(factor, center, 20.0, maxHz);
    m_spectrogramDirty = true;
}

void App::fitFrequency() {
    const double maxHz = std::min(
        20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
    m_spectralView.fit(20.0, maxHz);
    m_spectrogramDirty = true;
}

void App::selectTrack(int index) { m_tracks.select(index); }

void App::removeTrack(int index) {
    if (m_recordingController.recording()) stopRecording();
    m_tracks.remove(index);
}

void App::syncTrackControls() {
    m_masteringController.clear();
    m_tracks.syncControls();
}

void App::addEmptyTrack() { m_tracks.addEmpty(); }

std::shared_ptr<const vwave::Audio> App::activeAudioSnapshot() {
    if (!hasTracks()) return {};
    if (!m_audioSnapshot) {
        m_audioSnapshot =
            std::make_shared<vwave::Audio>(activeDocument().audio());
    }
    return m_audioSnapshot;
}

void App::cancelRenderJobs() {
    m_imageController.cancel();
    m_scopeController.cancel();
    m_wavetableController.cancel();
}

void App::toggleRecording() {
    m_recordingController.toggle(m_settingsController.settings().inputDevice);
}

void App::startRecording() {
    m_recordingController.start(m_settingsController.settings().inputDevice);
}

void App::updateRecording() { m_recordingController.update(); }

void App::stopRecording() { m_recordingController.stop(); }

void App::syncSelectionFromActive() {
    if (!hasTracks()) return;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const auto& selection = track.document.selection();
    if (selection.active && selection.end > selection.start) {
        m_timeline.setSelection(selection.start + track.startFrame,
                                selection.end + track.startFrame);
    } else {
        m_timeline.clearSelection();
    }
}

void App::updateMixer() {
    m_masteringController.clear();
    if (m_player.isPlaying()) m_player.updateMix(m_timeline);
}

vwave::StretchAlgorithm App::selectedStretchAlgorithm() const {
    switch (m_stretchAlgorithm) {
    case 1: return vwave::StretchAlgorithm::PhaseVocoder;
    case 2: return vwave::StretchAlgorithm::PaulStretch;
    default: return vwave::StretchAlgorithm::Wsola;
    }
}

void App::addButton(const std::string& label, int x, int y, int w, int h,
                    std::function<void()> action, bool accent, bool active,
                    bool enabled) {
    if (m_buildingSidebar) {
        addSidebarButton(label, x, y, w, h, std::move(action), accent, active,
                         enabled);
        return;
    }
    if (m_buildingTrackHeader) {
        m_trackHeaderControls.addButton(label, {x, y, w, h},
                                        std::move(action), accent, active,
                                        enabled);
        return;
    }
}

int App::textWidth(const std::string& text, TTF_Font* font) const {
    return m_ui.textWidth(text, font);
}

int App::drawText(const std::string& text, int x, int y, const Color& color,
                  TTF_Font* font) {
    return m_ui.drawText(text, x, y, color, font);
}

void App::drawTextCentered(const std::string& text, const SDL_Rect& rect,
                           const Color& color, TTF_Font* font) {
    m_ui.drawTextCentered(text, rect, color, font);
}

void App::drawSection(const std::string& title, int& y) {
    m_ui.drawSection(title, m_sidebarRect.w, y);
}

void App::analyzeMasteringStems() {
    m_masteringController.analyze(
        static_cast<int>(std::lround(m_masteringBands.value)),
        m_masteringMaxCut.value, m_masteringMaxBoost.value,
        static_cast<int>(std::lround(m_masteringSmoothing.value)));
}

void App::saveMasteringReport(const std::string& path) {
    m_masteringController.saveReport(path);
}

void App::renderMasteringStems() {
    m_masteringController.render(m_masteringCeiling.value);
}

void App::regenerateActiveTrack() {
    if (m_timeline.empty()) return;
    const int index = m_timeline.activeTrack();
    if (index < 0 || index >= m_timeline.trackCount()) return;
    if (m_timeline.track(index).recipe.empty()) {
        setStatus("Active track has no recipe");
        return;
    }
    vwave::Audio regenerated;
    std::string error;
    if (!vwave::regenerateTrack(m_timeline, index, regenerated, &error)) {
        setStatus(error.empty() ? "Regenerate failed" : error, 6000);
        return;
    }
    m_player.stop();
    const int64_t start = m_timeline.track(index).startFrame;
    if (!m_timeline.setTrackAudio(index, std::move(regenerated), start,
                                  &error)) {
        setStatus("Regenerate failed: " + error, 6000);
        return;
    }
    clearAnalysis();
    setStatus("Regenerated track " + m_timeline.track(index).name);
}

void App::toggleSelectionScope() { m_selection.toggleScope(); }

void App::cropActiveTrack() { m_selection.cropActiveTrack(); }

void App::shiftActiveTrack(double seconds) {
    m_selection.shiftActiveTrack(seconds);
}

void App::repeatActiveSelection(int count) {
    m_selection.repeatSelection(count);
}

void App::moveActiveSelectionToPlayhead(bool copy) {
    m_selection.moveSelectionToPlayhead(copy);
}

void App::zeroCrossCut() { m_selection.zeroCrossCut(); }

void App::snapActiveSelection() { m_selection.snapSelection(); }

void App::stretchActiveTrack() {
    m_selection.stretchTrack(selectedStretchAlgorithm());
}

void App::splitActiveStereoTrack() { m_stereo.splitActive(); }

void App::combineActiveMono(int direction) {
    m_stereo.combineActive(direction);
}

void App::cycleStereoPadding() { m_stereo.cyclePadding(); }

void App::toggleClipTool() { m_clips.toggleTool(); }

void App::flattenClips() { m_clips.flatten(); }

void App::splitClipAtPlayhead() { m_clips.splitAtPlayhead(); }

void App::deleteSelectedClip() { m_clips.deleteSelected(); }

void App::repeatSelectedClip(int count) { m_clips.repeatSelected(count); }

void App::moveSelectedClipToPlayhead() { m_clips.moveSelectedToPlayhead(); }

void App::stretchSelectedClip() { m_clips.stretchSelected(); }

int App::currentClipIndex() const { return m_clips.currentIndex(); }

bool App::seekToTime(double seconds, std::string* error) {
    requestPrefetch();
    return m_playback.seekToTime(seconds, error);
}

void App::goToStart() { m_playback.goToStart(); }

void App::goToEnd() { m_playback.goToEnd(); }

void App::setLoopToSelection() {
    if (!m_timeline.selection().active) {
        setStatus("Select a range first");
        return;
    }
    const auto selection = m_timeline.selectedRange();
    setLoopRegion(selection.start, selection.end);
    setStatus("Loop set to selection");
}

void App::clearLoop() {
    const bool wasPlaying = m_player.isPlaying();
    m_loop.clear();
    if (wasPlaying) {
        m_player.stop();
        togglePlay();
    }
    setStatus("Loop cleared");
}

void App::toggleLoop() {
    if (m_loop.enabled()) {
        clearLoop();
        return;
    }
    const int64_t total = m_timeline.lengthFrames();
    const int64_t start = std::clamp<int64_t>(m_loop.start(), 0, total);
    const int64_t end = std::clamp<int64_t>(m_loop.end(), 0, total);
    if (end > start) {
        const bool wasPlaying = m_player.isPlaying();
        m_loop.setEnabled(true);
        if (wasPlaying) {
            m_player.stop();
            togglePlay();
        }
        setStatus("Loop enabled");
        return;
    }
    if (m_timeline.selection().active) {
        setLoopToSelection();
        setStatus("Loop enabled");
        return;
    }
    setStatus("Select a range or shift-drag the timeline first");
}

bool App::loopPlaybackActive() const {
    return m_playback.loopPlaybackActive();
}

void App::setLoopRegion(int64_t start, int64_t end) {
    const int64_t total = m_timeline.lengthFrames();
    start = std::clamp<int64_t>(start, 0, total);
    end = std::clamp<int64_t>(end, 0, total);
    if (end <= start) {
        clearLoop();
        return;
    }
    const bool wasPlaying = m_player.isPlaying();
    m_loop.setRegion(start, end, total);
    if (wasPlaying) {
        m_player.stop();
        togglePlay();
    }
    setStatus("Loop " +
              formatSeconds(static_cast<double>(start) /
                            std::max(1, m_timeline.sampleRate())) +
              " - " +
              formatSeconds(static_cast<double>(end) /
                            std::max(1, m_timeline.sampleRate())));
}


void App::cycleImagePhaseMode() { m_imageController.cyclePhaseMode(); }

void App::cycleImageFrequencyScale() {
    m_imageController.cycleFrequencyScale();
}

void App::cycleImageTimeMapping() { m_imageController.cycleTimeMapping(); }

void App::loadImage(const std::string& path) { m_imageController.load(path); }

void App::renderImageToTrack() {
    vwave::app::ImageRenderParams params;
    params.duration = m_imageDuration.value;
    params.minHz = m_imageMinHz.value;
    params.maxHz = m_imageMaxHz.value;
    params.gamma = m_imageGamma.value;
    params.contrast = m_imageContrast.value;
    params.threshold = m_imageThreshold.value;
    params.loops = static_cast<int>(std::lround(m_imageLoops.value));
    params.iterations =
        static_cast<int>(std::lround(m_imageIterations.value));
    m_imageController.render(m_playhead, params);
}

vwave::app::ScopeShapeParams App::scopeShapeParams() const {
    vwave::app::ScopeShapeParams params;
    params.pointCount =
        static_cast<int>(std::lround(m_scopePointCount.value));
    params.size = m_scopeSize.value;
    params.a = m_scopeA.value;
    params.b = m_scopeB.value;
    params.phase = m_scopePhase.value;
    params.turns = static_cast<int>(std::lround(m_scopeTurns.value));
    return params;
}

vwave::app::ScopeRenderParams App::scopeRenderParams() const {
    vwave::app::ScopeRenderParams params;
    params.duration = m_scopeDuration.value;
    params.scanHz = m_scopeScanHz.value;
    params.gain = m_scopeGain.value;
    params.smoothing = m_scopeSmoothing.value;
    return params;
}

void App::generateScopePath() {
    m_scopeController.generate(scopeShapeParams());
}

void App::cycleScopeShape() {
    m_scopeController.cycleShape(scopeShapeParams());
}

void App::cycleScopeDirection() { m_scopeController.cycleDirection(); }

void App::cycleScopeOversampling() { m_scopeController.cycleOversampling(); }

void App::renderScopeToTrack() {
    m_scopeController.render(m_playhead, scopeRenderParams());
}

void App::loadScopePathFile(const std::string& path) {
    if (m_scopeController.load(path)) {
        syncScopeKeyframeControls();
        setTab(vwave::EditorTab::Oscilloscope);
    }
}

void App::saveScopePathFile(const std::string& path) {
    m_scopeController.setDuration(m_scopeDuration.value);
    m_scopeController.save(path);
}

void App::toggleScopeXYMode() {
    m_scopeXYMode = !m_scopeXYMode;
    m_scopeXYDirty = true;
    setStatus(m_scopeXYMode ? "XY audio verification" : "Scope path design");
}

void App::importScopeImage(const std::string& path) {
    m_scopeController.importImage(path);
    setTab(vwave::EditorTab::Oscilloscope);
}

void App::importScopeSvg(const std::string& path) {
    m_scopeController.importSvg(path);
    setTab(vwave::EditorTab::Oscilloscope);
}

void App::syncScopeKeyframeControls() {
    const vwave::ScopeTransform transform =
        m_scopeController.selectedTransform();
    m_scopeKeyTime.setValue(m_scopeController.selectedKeyframeTime());
    m_scopeTranslateX.setValue(transform.translateX);
    m_scopeTranslateY.setValue(transform.translateY);
    m_scopeRotation.setValue(transform.rotation * 180.0 / 3.14159265358979323846);
    m_scopeScale.setValue(transform.scaleX);
}


void App::toggleScopePreview() {
    if (m_scopePreviewPlaying) {
        m_player.stop();
        m_scopePreviewPlaying = false;
        m_scopePreviewAudio.reset();
        m_scopeXYDirty = true;
        setStatus("Scope preview stopped");
        return;
    }
    m_scopeController.renderPreview(
        scopeRenderParams(),
        [this](vwave::Audio audio) {
            if (audio.empty()) {
                setStatus("Scope preview is empty");
                return;
            }
            m_scopePreviewAudio =
                std::make_shared<vwave::Audio>(std::move(audio));
            ++m_scopePreviewRevision;
            m_scopeXYMode = true;
            m_scopeXYDirty = true;
            const int sampleRate =
                std::max(1, m_scopePreviewAudio->sampleRate());
            const int64_t frames = m_scopePreviewAudio->numFrames();
            int64_t start = 0;
            int64_t end = frames;
            const bool loop = m_scopeLoopActive &&
                              m_scopeLoopEndTime > m_scopeLoopStartTime;
            if (loop) {
                start = std::clamp<int64_t>(
                    static_cast<int64_t>(std::llround(
                        m_scopeLoopStartTime * sampleRate)),
                    0, frames);
                end = std::clamp<int64_t>(
                    static_cast<int64_t>(std::llround(
                        m_scopeLoopEndTime * sampleRate)),
                    start + 1, frames);
            }
            m_player.stop();
            const bool playing =
                m_player.playAudio(m_scopePreviewAudio, start, end, loop);
            m_scopePreviewPlaying = playing;
            m_scopePreviewTime = start;
            m_scopeKeyTime.setValue(
                static_cast<double>(start) / static_cast<double>(sampleRate));
            if (!playing) {
                setStatus("Scope preview ready; audio playback unavailable");
            } else {
                setStatus(loop ? "Playing scope preview loop"
                               : "Playing scope preview");
            }
        });
}

void App::setTab(vwave::EditorTab tab) {
    if (m_tab != tab) m_sidebarScroll = 0.0;
    closeContextMenu();
    m_tab = tab;
    m_spectrum3DHoverX = -1;
    m_spectrum3DHoverY = -1;
    if (tab == vwave::EditorTab::Spectrum) m_spectrogramDirty = true;
}

void App::togglePlay() {
    if (m_recordingController.recording()) stopRecording();
    requestPrefetch();
    m_playback.togglePlay();
}

void App::stopPlayback() { m_playback.stop(); }

void App::updatePlayback() {
    updateWaveDragAutoscroll();
    if (m_scopePreviewPlaying) {
        if (m_player.isPlaying()) {
            m_scopePreviewTime = m_player.sourcePosition();
            if (m_scopePreviewAudio &&
                m_scopePreviewAudio->sampleRate() > 0) {
                m_scopeKeyTime.setValue(
                    static_cast<double>(m_scopePreviewTime) /
                    static_cast<double>(m_scopePreviewAudio->sampleRate()));
            }
        } else {
            m_scopePreviewPlaying = false;
        }
        return;
    }
    m_playback.update();
}

void App::runEdit(const std::function<bool(std::string*)>& edit,
                  const std::string& success) {
    m_player.stop();
    std::string error;
    if (edit(&error)) {
        markEdited();
        setStatus(success);
    } else {
        setStatus(error.empty() ? "Edit failed" : error);
    }
}

void App::markEdited() {
    m_fileLoader.cancel();
    m_masteringController.clear();
    m_phaseSpectrogramDirty = true;
    m_audioSnapshot.reset();
    m_analysis.cancelAll();
    m_loop.clamp(m_timeline.lengthFrames());
    if (hasTracks())
        m_clips.afterEdit(activeDocument());
    else
        m_clips.reset();
    syncSelectionFromActive();
    m_view.clamp(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    m_overviewDirty = true;
    m_spectrogramDirty = true;
    m_analysisData.clear();
    m_spectralController.clear();
    markDirty();
}

void App::markDirty() {
    ++m_editGeneration;
    ++m_prefetchGeneration;
    m_modified = true;
    m_autosavePending = true;
    m_lastEditTicks = SDL_GetTicks();
    updateWindowTitle();
}

void App::clearAnalysis() {
    m_masteringController.clear();
    m_phaseSpectrogramDirty = true;
    m_audioSnapshot.reset();
    m_analysis.cancelAll();
    m_analysisData.clear();
    m_overviewDirty = true;
    m_spectrogramDirty = true;
    m_spectralController.clear();
}

void App::analyzeBeats() {
    const auto audio = activeAudioSnapshot();
    if (!audio || audio->empty()) {
        setStatus("Open an audio file first");
        return;
    }
    m_analysisData.analyzeBeats(audio, m_beatSensitivity.value);
}

void App::detectPitch() {
    const auto audio = activeAudioSnapshot();
    if (!audio || audio->empty()) {
        setStatus("Open an audio file first");
        return;
    }
    const vwave::Range range = activeDocument().selection().active
                                   ? activeDocument().selectedRange()
                                   : vwave::Range{};
    m_analysisData.detectPitch(audio, range);
}

void App::sliceCurrent() {
    if (activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    std::vector<vwave::Slice> slices;
    if (m_sliceMode == 0) {
        slices = vwave::sliceByTransients(
            activeDocument().audio(), m_sliceSensitivity.value,
            m_minSliceMs.value / 1000.0);
    } else if (m_sliceMode == 1) {
        const vwave::BeatAnalysis analysis =
            m_analysisData.hasBeats()
                ? m_analysisData.beats()
                : vwave::analyzeBeats(
                      activeDocument().audio(), m_beatSensitivity.value);
        if (analysis.bpm <= 0.0) {
            setStatus("No tempo available for beat slicing");
            return;
        }
        slices = vwave::sliceByBeats(
            activeDocument().audio(), analysis.bpm,
            static_cast<int>(std::lround(m_beatsPerSlice.value)));
    } else {
        std::vector<int64_t> markers;
        for (const auto& marker : activeDocument().markers())
            markers.push_back(marker.frame);
        slices = vwave::sliceByMarkers(activeDocument().audio(), markers);
    }
    if (slices.empty()) {
        setStatus("No slices found");
        return;
    }
    activeDocument().setSlices(std::move(slices));
    setStatus("Created " + std::to_string(activeDocument().slices().size()) +
              " slices");
}

void App::stretchSelection(bool toNewTrack) {
    if (!hasTracks() || activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    if (!toNewTrack) {
        runEdit([this](std::string* error) {
            return activeDocument().timeStretch(
                m_stretchRatio.value, selectedStretchAlgorithm(), error);
        }, "Time stretch applied in place");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const vwave::Range range = activeDocument().selection().active
                                   ? activeDocument().selectedRange()
                                   : vwave::Range{0, activeDocument().numFrames()};
    if (range.empty()) {
        setStatus("Select a range to process");
        return;
    }
    const vwave::Audio selected = activeDocument().audio().slice(range);
    const vwave::Audio processed = vwave::timeStretch(
        selected, m_stretchRatio.value, selectedStretchAlgorithm());
    if (processed.empty()) {
        setStatus("Time stretch failed");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.addAudioTrack(track.name + " stretch", processed,
                                  track.startFrame + range.start, &error)) {
        setStatus("Cannot create processed track: " + error);
        return;
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = track.startFrame + range.start;
    m_tab = vwave::EditorTab::Waveform;
    setStatus("Time-stretched selection rendered to a new track");
}

void App::pitchSelection(bool toNewTrack) {
    if (!hasTracks() || activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    if (!toNewTrack) {
        runEdit([this](std::string* error) {
            return activeDocument().pitchShift(
                m_pitchSemitones.value, selectedStretchAlgorithm(), error);
        }, "Pitch shift applied in place");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const vwave::Range range = activeDocument().selection().active
                                   ? activeDocument().selectedRange()
                                   : vwave::Range{0, activeDocument().numFrames()};
    if (range.empty()) {
        setStatus("Select a range to process");
        return;
    }
    const vwave::Audio selected = activeDocument().audio().slice(range);
    const vwave::Audio processed = vwave::pitchShift(
        selected, m_pitchSemitones.value, selectedStretchAlgorithm());
    if (processed.empty()) {
        setStatus("Pitch shift failed");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.addAudioTrack(track.name + " pitch", processed,
                                  track.startFrame + range.start, &error)) {
        setStatus("Cannot create processed track: " + error);
        return;
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = track.startFrame + range.start;
    m_tab = vwave::EditorTab::Waveform;
    setStatus("Pitch-shifted selection rendered to a new track");
}

void App::copySelectionToNewTrack() {
    if (!hasTracks() || !activeDocument().selection().active) {
        setStatus("Select a range first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const vwave::Range range = activeDocument().selectedRange();
    if (range.empty()) return;
    vwave::Audio selected = activeDocument().audio().slice(range);
    m_player.stop();
    std::string error;
    if (!m_timeline.addAudioTrack(track.name + " selection", selected,
                                  track.startFrame + range.start, &error)) {
        setStatus("Cannot create track: " + error);
        return;
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = track.startFrame + range.start;
    m_tab = vwave::EditorTab::Waveform;
    setStatus("Selection copied to a new track");
}

vwave::Range App::spectralTimeRange() const {
    return m_spectralController.timeRange();
}

void App::applySpectralGain(double db) {
    m_spectralController.applyGain(db);
}

void App::applySpectralFilter(bool lowpass) {
    m_spectralController.applyFilter(lowpass, m_spectralCutoff.value);
}

void App::applySpectralGate() {
    m_spectralController.applyGate(m_spectralGate.value);
}

void App::applySpectralBlur() {
    m_spectralController.applyBlur(
        static_cast<int>(std::lround(m_spectralBlurTime.value)),
        static_cast<int>(std::lround(m_spectralBlurFrequency.value)));
}

void App::addMarkerAtPlayhead() { m_selection.addMarkerAtPlayhead(); }

bool App::loadFile(const std::string& path) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".vwdoc") return loadProject(path);
    if (isScopePath(path)) {
        loadScopePathFile(path);
        return m_scopeController.valid();
    }
    if (isImagePath(path)) {
        return m_imageController.load(path);
    }
    return loadAudio(path);
}

bool App::loadAudio(const std::string& path) {
    if (m_recordingController.recording()) stopRecording();
    m_player.stop();
    cancelRenderJobs();
    m_fileSaver.cancel();
    m_autosavePending = false;
    m_autosaveInProgress = false;
    return m_fileLoader.loadAudio(
        path, [this](vwave::Audio audio, const std::string& loadedPath) {
            m_player.stop();
            vwave::Timeline timeline;
            std::string error;
            std::string name =
                std::filesystem::path(loadedPath).stem().string();
            if (name.empty()) name = "Track 1";
            if (!timeline.addAudioTrack(name, audio, 0, &error)) {
                setStatus("Open failed: " + error, 6000);
                return;
            }
            m_timeline = std::move(timeline);
            m_timeline.clearUndoHistory();
            m_audioPath = loadedPath;
            m_projectPath.clear();
            m_modified = false;
            m_autosavePending = false;
            m_autosaveInProgress = false;
            ++m_editGeneration;
            ++m_prefetchGeneration;
            m_prefetch.cancel();
            m_scopeXYDirty = true;
            m_scopePreviewAudio.reset();
            m_scopePreviewPlaying = false;
            m_scopeLoopActive = false;
            m_scopeLoopStartTime = 0.0;
            m_scopeLoopEndTime = 0.0;
            m_clips.reset();
            m_loop.clear();
            syncTrackControls();
            m_wavetableController.clear();
            m_view.fit(m_timeline.lengthFrames());
            m_spectralView.maxHz = std::min(
                20000.0,
                std::max(1000.0, m_timeline.sampleRate() / 2.0));
            clearAnalysis();
            m_playhead = 0;
            updateWindowTitle();
            setStatus("Loaded " + baseName(loadedPath));
        });
}

bool App::addTrackFile(const std::string& path) {
    if (m_timeline.empty()) return loadAudio(path);
    if (m_recordingController.recording()) stopRecording();
    m_player.stop();
    return m_fileLoader.loadAudio(
        path, [this](vwave::Audio audio, const std::string& loadedPath) {
            m_player.stop();
            std::string name =
                std::filesystem::path(loadedPath).stem().string();
            if (name.empty())
                name = "Track " +
                       std::to_string(m_timeline.trackCount() + 1);
            std::string error;
            if (!m_timeline.addAudioTrack(name, audio, 0, &error)) {
                setStatus("Add track failed: " + error, 6000);
                return;
            }
            m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
            m_clips.reset();
            syncTrackControls();
            clearAnalysis();
            m_view.fit(m_timeline.lengthFrames());
            m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
            markEdited();
            setStatus("Added track " + name);
        });
}

bool App::loadProject(const std::string& path) {
    if (m_recordingController.recording()) stopRecording();
    m_player.stop();
    cancelRenderJobs();
    m_fileSaver.cancel();
    m_autosavePending = false;
    m_autosaveInProgress = false;
    return m_fileLoader.loadProject(
        path, [this](vwave::Timeline timeline, const std::string& loadedPath) {
            applyLoadedProject(std::move(timeline), loadedPath);
        });
}

void App::applyLoadedProject(vwave::Timeline timeline,
                             const std::string& projectPath) {
    m_player.stop();
    m_timeline = std::move(timeline);
    m_projectPath = projectPath;
    m_audioPath =
        hasTracks() ? activeDocument().sourcePath() : std::string{};
    m_modified = false;
    m_autosavePending = false;
    m_autosaveInProgress = false;
    ++m_editGeneration;
    ++m_prefetchGeneration;
    m_prefetch.cancel();
    m_scopeXYDirty = true;
    m_scopePreviewAudio.reset();
    m_scopePreviewPlaying = false;
    m_scopeLoopActive = false;
    m_scopeLoopStartTime = 0.0;
    m_scopeLoopEndTime = 0.0;
    m_clips.reset();
    m_loop.clear();
    m_wavetableController.clear();
    syncTrackControls();
    m_view.fit(m_timeline.lengthFrames());
    m_spectralView.maxHz = std::min(
        20000.0,
        std::max(1000.0, m_timeline.sampleRate() / 2.0));
    clearAnalysis();
    m_playhead = 0;
    updateWindowTitle();
    const bool recoveryAvailable =
        std::filesystem::exists(m_projectPath + ".autosave");
    setStatus("Loaded project " + baseName(projectPath) +
              (recoveryAvailable ? " (autosave available)" : ""));
}

void App::saveAudio(const std::string& path) {
    m_fileSaver.saveAudio(
        m_timeline, path, [this](const std::string& target) {
            m_audioPath = target;
            updateWindowTitle();
        });
}

void App::saveProject(const std::string& path) {
    const uint64_t generation = m_editGeneration;
    m_autosavePending = false;
    m_autosaveInProgress = false;
    m_fileSaver.saveProject(
        m_timeline, path, [this, generation](const std::string& target) {
            m_projectPath = target;
            if (generation == m_editGeneration) {
                m_modified = false;
                m_autosavePending = false;
                removeAutosaveFiles(target);
            } else {
                m_autosavePending = true;
            }
            updateWindowTitle();
        });
}

void App::requestPrefetch() {
    if (!hasTracks()) return;
    const uint64_t generation = m_prefetchGeneration;
    m_prefetch.prefetch(
        m_timeline, generation,
        [this](uint64_t completedGeneration,
               std::vector<PrefetchedBlock> blocks) {
            if (completedGeneration != m_prefetchGeneration) return;
            for (auto& block : blocks) {
                if (block.trackIndex < 0 ||
                    block.trackIndex >= m_timeline.trackCount())
                    continue;
                m_timeline.track(block.trackIndex)
                    .document.cachePrefetchedBlock(block.block, block.audio);
            }
        });
}

void App::removeAutosaveFiles(const std::string& projectPath) {
    if (projectPath.empty()) return;
    std::error_code ec;
    std::filesystem::remove(projectPath + ".autosave", ec);
    std::filesystem::remove_all(projectPath + ".autosave.blocks", ec);
}

void App::updateAutosave() {
    if (!m_autosavePending || m_autosaveInProgress || m_projectPath.empty())
        return;
    const Uint32 now = SDL_GetTicks();
    if (now < m_autosaveRetryAfter) return;
    constexpr Uint32 kAutosaveDelayMs = 2500;
    if (now - m_lastEditTicks < kAutosaveDelayMs) return;

    const uint64_t generation = m_editGeneration;
    const std::string target = m_projectPath + ".autosave";
    m_autosaveInProgress = true;
    m_fileSaver.saveAutosave(
        m_timeline, target,
        [this, target, generation](bool success, const std::string& error) {
            m_autosaveInProgress = false;
            if (target != m_projectPath + ".autosave") return;
            if (success) {
                if (generation == m_editGeneration)
                    m_autosavePending = false;
                setStatus("Autosave ready", 1200);
            } else {
                m_autosaveRetryAfter = SDL_GetTicks() + 5000;
                if (!error.empty())
                    setStatus("Autosave failed: " + error, 4000);
            }
        });
}

void App::recoverAutosave() {
    if (m_projectPath.empty()) {
        setStatus("Save the project before recovering");
        return;
    }
    const std::string recovery = m_projectPath + ".autosave";
    if (!std::filesystem::exists(recovery)) {
        setStatus("No autosave available");
        return;
    }
    const std::string projectPath = m_projectPath;
    const std::string recoveryPath = recovery;
    m_fileSaver.cancel();
    m_autosavePending = false;
    m_autosaveInProgress = false;
    m_fileLoader.loadProject(
        recoveryPath,
        [this, projectPath](vwave::Timeline timeline,
                            const std::string& /*loadedPath*/) {
            applyLoadedProject(std::move(timeline), projectPath);
            m_modified = true;
            m_autosavePending = true;
            ++m_editGeneration;
            ++m_prefetchGeneration;
            m_lastEditTicks = SDL_GetTicks();
            updateWindowTitle();
            setStatus("Recovered autosave; save project to keep changes", 6000);
        });
}

void App::exportSlices(const std::string& directory) {
    if (activeDocument().slices().empty()) {
        setStatus("Create slices first");
        return;
    }
    std::string prefix = baseName(m_audioPath).empty()
                             ? "slice"
                             : std::filesystem::path(baseName(m_audioPath))
                                   .stem()
                                   .string();
    m_fileSaver.exportSlices(
        activeDocument().audio(), activeDocument().slices(), directory,
        std::move(prefix),
        [](size_t, const std::string&) {});
}

void App::extractWavetable() {
    if (activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    vwave::WavetableExtractOptions options;
    options.frameSize = kFrameSizes[m_frameSizeIndex];
    options.frameCount =
        static_cast<int>(std::lround(m_wavetableFrameCount.value));
    if (activeDocument().selection().active)
        options.range = activeDocument().selectedRange();
    options.name = baseName(m_audioPath);
    m_wavetableController.extract(activeDocument().audio(), options);
    if (!m_wavetableController.valid()) return;
    m_wavetableController.table().setRootNote(m_tableRootControl.value);
    syncWavetableDesignControls();
    setTab(vwave::EditorTab::Wavetable);
}

void App::loadWavetable(const std::string& path) {
    m_wavetableController.load(path);
}

void App::saveWavetable(const std::string& path) {
    m_wavetableController.save(path, activeDocument().sampleRate());
}

void App::syncWavetableDesignControls() {
    auto& table = m_wavetableController.table();
    if (!table.valid()) return;
    m_wavetableController.syncWeights();
    const int frame = m_wavetableController.view().selectedFrame;
    m_framePositionControl.setValue(table.framePosition(frame));
    m_frameWeightControl.setValue(
        m_wavetableController.weights()[static_cast<size_t>(frame)]);
    m_tableRootControl.setValue(table.rootNote());
}

void App::addWavetableFrame(bool duplicate) {
    m_wavetableController.addFrame(duplicate);
    syncWavetableDesignControls();
}

void App::addHarmonicWavetableFrame() {
    m_wavetableController.addHarmonicFrame(
        static_cast<int>(std::lround(m_harmonicCount.value)));
    syncWavetableDesignControls();
}

void App::deleteWavetableFrame() {
    m_wavetableController.deleteFrame();
    syncWavetableDesignControls();
}

void App::cycleWavetableMorphMode() {
    m_wavetableController.cycleMorphMode();
    syncWavetableDesignControls();
}

void App::cycleWavetableInterpolation() {
    m_wavetableController.cycleInterpolation();
    syncWavetableDesignControls();
}

void App::cycleWavetableOversampling() {
    m_wavetableController.cycleOversampling();
}

void App::renderWavetableNote() {
    vwave::app::WavetableNoteParams params;
    params.note = static_cast<float>(m_synthNote.value);
    params.rootNote = m_synthRoot.value;
    params.duration = m_synthDuration.value;
    params.morph = m_synthMorph.value;
    params.gain = m_synthGain.value;
    params.unison = static_cast<int>(std::lround(m_synthUnison.value));
    m_wavetableController.render(params);
}

void App::openDialog(DialogMode mode, const std::string& title,
                     const std::string& initialPath, bool save) {
    m_openMenu = -1;
    m_settingsController.close();
    m_dialog.open(mode, title, initialPath, save);
}

void App::applyDialog(DialogMode mode, const std::string& path) {
    if (path.empty()) {
        setStatus("Path is empty");
        return;
    }
    switch (mode) {
    case DialogMode::Open:
        loadFile(path);
        break;
    case DialogMode::AddTrack:
        addTrackFile(path);
        break;
    case DialogMode::SaveAudio:
        saveAudio(path);
        break;
    case DialogMode::SaveProject:
        saveProject(path);
        break;
    case DialogMode::SaveWavetable:
        saveWavetable(path);
        break;
    case DialogMode::LoadWavetable:
        loadWavetable(path);
        break;
    case DialogMode::SaveMasteringReport:
        saveMasteringReport(path);
        break;
    case DialogMode::LoadImage:
        loadImage(path);
        break;
    case DialogMode::SaveScopePath:
        saveScopePathFile(path);
        break;
    case DialogMode::LoadScopePath:
        loadScopePathFile(path);
        break;
    case DialogMode::ImportScopeImage:
        importScopeImage(path);
        break;
    case DialogMode::ImportScopeSvg:
        importScopeSvg(path);
        break;
    case DialogMode::SeekTime: {
        double seconds = 0.0;
        if (!vwave::parseTimecode(path, seconds)) {
            setStatus("Invalid time: " + path);
            break;
        }
        std::string error;
        if (!seekToTime(seconds, &error)) {
            setStatus(error.empty() ? "Cannot seek" : error);
        }
        break;
    }
    case DialogMode::ExportSlices:
        exportSlices(path);
        break;
    case DialogMode::None:
        break;
    }
}
