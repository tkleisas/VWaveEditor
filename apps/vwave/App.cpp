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
              updateWindowTitle();
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
          m_timeline, m_player,
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
          }),
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
          }),
      m_settingsController(
          m_player, [this](const std::string& status) { setStatus(status); },
          [this](const std::string& theme) { applyPalette(theme); }) {
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
    if (m_spectrogramTexture) SDL_DestroyTexture(m_spectrogramTexture);
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
    int frames = 0;
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) handleEvent(event);
        updatePlayback();
        updateRecording();
        m_dispatcher.drain();
        draw();
        ++frames;
        if (smokeTest && frames >= 4) break;
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
        "VWaveEditor " VWAVE_VERSION " - " + baseName(name);
    SDL_SetWindowTitle(m_window, title.c_str());
}

void App::draw() {
    layout();
    m_ui.beginFrame();
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
    if (m_dialog.isOpen())
        m_dialog.draw(m_ui, m_toolbarRect.w,
                      m_footerRect.y + m_footerRect.h);
    SDL_RenderPresent(m_renderer);
}

void App::drawButton(const Button& button) {
    m_ui.drawButton(button);
}

void App::addSlider(const std::string& label, const std::string& valueText,
                    int x, int y, int w, vwave::ParameterSlider& slider,
                    std::function<void(double)> onChanged) {
    m_ui.addSlider(label, valueText, x, y, w, slider, std::move(onChanged));
}

void App::setSliderFromX(SliderRef& slider, int x) {
    m_ui.setSliderFromX(slider, x);
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
    m_ui.addButton(label, x, y, w, h, std::move(action), accent, active,
                   enabled);
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

void App::makeClips() { m_clips.make(); }

void App::flattenClips() { m_clips.flatten(); }

void App::splitClipAtPlayhead() { m_clips.splitAtPlayhead(); }

void App::deleteSelectedClip() { m_clips.deleteSelected(); }

void App::repeatSelectedClip(int count) { m_clips.repeatSelected(count); }

void App::moveSelectedClipToPlayhead() { m_clips.moveSelectedToPlayhead(); }

void App::stretchSelectedClip() { m_clips.stretchSelected(); }

int App::currentClipIndex() const { return m_clips.currentIndex(); }

bool App::seekToTime(double seconds, std::string* error) {
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

void App::loadImage(const std::string& path) {
    if (m_imageController.load(path)) setTab(vwave::EditorTab::Image);
}

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
    if (m_scopeController.load(path))
        setTab(vwave::EditorTab::Oscilloscope);
}

void App::saveScopePathFile(const std::string& path) {
    m_scopeController.save(path);
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
    m_playback.togglePlay();
}

void App::stopPlayback() { m_playback.stop(); }

void App::updatePlayback() { m_playback.update(); }

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
        loadImage(path);
        return m_imageController.valid();
    }
    return loadAudio(path);
}

bool App::loadAudio(const std::string& path) {
    if (m_recordingController.recording()) stopRecording();
    m_player.stop();
    vwave::Audio audio;
    std::string error;
    if (!vwave::readAudio(path, audio, &error)) {
        setStatus("Open failed: " + error, 6000);
        return false;
    }
    cancelRenderJobs();
    vwave::Timeline timeline;
    std::string name = std::filesystem::path(path).stem().string();
    if (name.empty()) name = "Track 1";
    if (!timeline.addAudioTrack(name, audio, 0, &error)) {
        setStatus("Open failed: " + error, 6000);
        return false;
    }
    m_timeline = std::move(timeline);
    m_audioPath = path;
    m_projectPath.clear();
    m_clips.reset();
    m_loop.clear();
    syncTrackControls();
    m_wavetableController.clear();
    m_view.fit(m_timeline.lengthFrames());
    m_spectralView.maxHz = std::min(
        20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
    clearAnalysis();
    m_playhead = 0;
    updateWindowTitle();
    setStatus("Loaded " + baseName(path));
    return true;
}

bool App::addTrackFile(const std::string& path) {
    if (m_timeline.empty()) return loadAudio(path);
    if (m_recordingController.recording()) stopRecording();
    m_player.stop();
    vwave::Audio audio;
    std::string error;
    if (!vwave::readAudio(path, audio, &error)) {
        setStatus("Add track failed: " + error, 6000);
        return false;
    }
    std::string name = std::filesystem::path(path).stem().string();
    if (name.empty()) name = "Track " +
                             std::to_string(m_timeline.trackCount() + 1);
    if (!m_timeline.addAudioTrack(name, audio, 0, &error)) {
        setStatus("Add track failed: " + error, 6000);
        return false;
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    m_clips.reset();
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    updateWindowTitle();
    setStatus("Added track " + name);
    return true;
}

bool App::loadProject(const std::string& path) {
    if (m_recordingController.recording()) stopRecording();
    m_player.stop();
    cancelRenderJobs();
    vwave::Timeline timeline;
    std::string error;
    if (!timeline.loadProject(path, &error)) {
        setStatus("Open failed: " + error, 6000);
        return false;
    }
    m_timeline = std::move(timeline);
    m_projectPath = path;
    m_audioPath = hasTracks() ? activeDocument().sourcePath() : std::string{};
    m_clips.reset();
    m_loop.clear();
    m_wavetableController.clear();
    syncTrackControls();
    m_view.fit(m_timeline.lengthFrames());
    m_spectralView.maxHz = std::min(
        20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
    clearAnalysis();
    m_playhead = 0;
    updateWindowTitle();
    setStatus("Loaded project " + baseName(path));
    return true;
}

void App::saveAudio(const std::string& path) {
    const std::string target = withExtension(path, ".wav");
    const vwave::Audio mix = m_timeline.mixdown();
    if (mix.empty()) {
        setStatus("Nothing to export");
        return;
    }
    std::string error;
    if (!vwave::writeAudio(target, mix, {}, &error)) {
        setStatus("Save failed: " + error, 6000);
        return;
    }
    m_audioPath = target;
    updateWindowTitle();
    setStatus("Saved " + baseName(target));
}

void App::saveProject(const std::string& path) {
    const std::string target = withExtension(path, ".vwdoc");
    std::string error;
    if (!m_timeline.saveProject(target, &error)) {
        setStatus("Save failed: " + error, 6000);
        return;
    }
    m_projectPath = target;
    updateWindowTitle();
    setStatus("Saved project " + baseName(target));
}

void App::exportSlices(const std::string& directory) {
    if (activeDocument().slices().empty()) {
        setStatus("Create slices first");
        return;
    }
    vwave::SliceExportOptions options;
    options.prefix = baseName(m_audioPath).empty()
                         ? "slice"
                         : std::filesystem::path(baseName(m_audioPath))
                               .stem()
                               .string();
    vwave::SliceExportResult result;
    std::string error;
    if (!vwave::exportSlices(activeDocument().audio(), activeDocument().slices(),
                             directory, options, &result, &error)) {
        setStatus("Export failed: " + error, 6000);
        return;
    }
    setStatus("Exported " + std::to_string(result.files.size()) + " slices");
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
    if (!m_wavetableController.load(path)) return;
    syncWavetableDesignControls();
    setTab(vwave::EditorTab::Wavetable);
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

void App::handleMouseDown(const SDL_MouseButtonEvent& event) {
    if (m_dialog.handleMouseDown(event.x, event.y)) return;
    if (m_settingsController.isOpen()) {
        if (event.button != SDL_BUTTON_LEFT) return;
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_settingsCloseRect)) m_settingsController.close();
        else if (SDL_PointInRect(&point, &m_settingsOutputPrev))
            m_settingsController.cycleOutputDevice(-1);
        else if (SDL_PointInRect(&point, &m_settingsOutputNext))
            m_settingsController.cycleOutputDevice(1);
        else if (SDL_PointInRect(&point, &m_settingsInputPrev))
            m_settingsController.cycleInputDevice(-1);
        else if (SDL_PointInRect(&point, &m_settingsInputNext))
            m_settingsController.cycleInputDevice(1);
        else if (SDL_PointInRect(&point, &m_settingsThemePrev))
            m_settingsController.setTheme(
            m_settingsController.settings().theme == "dark" ? "light"
                                                              : "dark");
        else if (SDL_PointInRect(&point, &m_settingsThemeNext))
            m_settingsController.setTheme(
            m_settingsController.settings().theme == "dark" ? "light"
                                                              : "dark");
        return;
    }
    if (event.button == SDL_BUTTON_RIGHT) {
        m_openMenu = -1;
        if (m_contextMenuOpen) {
            closeContextMenu();
            return;
        }
        if (m_tab == vwave::EditorTab::Waveform) {
            SDL_Point point{event.x, event.y};
            for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
                if (m_trackHeaderRects[i].w > 0 &&
                    SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
                    openTrackContextMenu(static_cast<int>(i), event.x,
                                         event.y);
                    return;
                }
            }
            for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
                if (m_trackLaneRects[i].w > 0 &&
                    SDL_PointInRect(&point, &m_trackLaneRects[i])) {
                    openTrackContextMenu(static_cast<int>(i), event.x,
                                         event.y);
                    return;
                }
            }
        }
        return;
    }
    if (m_contextMenuOpen) {
        if (event.button == SDL_BUTTON_LEFT) {
            SDL_Point point{event.x, event.y};
            if (SDL_PointInRect(&point, &m_contextMenuRect)) {
                for (size_t i = 0; i < m_contextMenuItemRects.size(); ++i) {
                    if (m_contextMenuItemRects[i].w > 0 &&
                        SDL_PointInRect(&point, &m_contextMenuItemRects[i]) &&
                        i < m_contextMenuItems.size() &&
                        m_contextMenuItems[i].enabled &&
                        !m_contextMenuItems[i].separator) {
                        auto action = m_contextMenuItems[i].action;
                        closeContextMenu();
                        if (action) action();
                        return;
                    }
                }
                return;
            }
            closeContextMenu();
        }
    }

    SDL_Point menuPoint{event.x, event.y};
    for (int index = 0; index < static_cast<int>(m_menus.size()); ++index) {
        if (SDL_PointInRect(&menuPoint, &m_menus[static_cast<size_t>(index)]
                                            .titleRect)) {
            m_openMenu = m_openMenu == index ? -1 : index;
            return;
        }
    }
    if (m_openMenu >= 0) {
        if (SDL_PointInRect(&menuPoint, &m_openMenuRect) && event.button ==
                                                               SDL_BUTTON_LEFT) {
            for (const auto& rect : m_menuItemRects) {
                if (rect.w > 0 && SDL_PointInRect(&menuPoint, &rect)) {
                    const size_t index = static_cast<size_t>(
                        &rect - m_menuItemRects.data());
                    MenuItem item = m_menus[static_cast<size_t>(m_openMenu)]
                                        .items[index];
                    m_openMenu = -1;
                    if (item.enabled && item.action) item.action();
                    return;
                }
            }
        }
        m_openMenu = -1;
        return;
    }
    if (event.button == SDL_BUTTON_LEFT) {
        SDL_Point point{event.x, event.y};
        const bool inSidebar = SDL_PointInRect(&point, &m_sidebarRect) != 0;
        const bool inToolbar = SDL_PointInRect(&point, &m_toolbarRect) != 0;
        const bool inContent = SDL_PointInRect(&point, &m_contentRect) != 0;
        const int area = inSidebar ? 1 : (inToolbar ? 0 : (inContent ? 2 : -1));
        if (area >= 0) {
            for (const auto& slider : m_ui.sliders()) {
                if (slider.area != area) continue;
                SDL_Rect hit{slider.track.x, slider.track.y - 12,
                             slider.track.w, slider.track.h + 20};
                if (SDL_PointInRect(&point, &hit)) {
                    m_activeSlider = slider;
                    m_sliderDragging = true;
                    setSliderFromX(m_activeSlider, event.x);
                    return;
                }
            }
        }
        if (area >= 0) {
            for (const auto& button : m_ui.buttons()) {
                if (button.enabled && button.area == area &&
                    SDL_PointInRect(&point, &button.rect)) {
                    button.action();
                    return;
                }
            }
        }
    }
    SDL_Point clickPoint{event.x, event.y};
    if (!SDL_PointInRect(&clickPoint, &m_contentRect)) return;
    if (event.button == SDL_BUTTON_MIDDLE) {
        m_panning = true;
        m_lastMouseX = event.x;
        return;
    }
    if (event.button != SDL_BUTTON_LEFT) return;
    if (m_tab == vwave::EditorTab::Waveform) {
        if (m_clips.toolEnabled() && !activeDocument().hasClips())
            m_clips.reset();
        m_clickCandidate = false;
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_rulerArea)) {
            const int64_t frame =
                laneFrameAtX(m_rulerArea, event.x);
            m_player.stop();
            m_playhead = clampFrame(frame, m_timeline.lengthFrames());
            m_timeline.clearSelection();
            if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                m_waveDrag = WaveDragMode::LoopSelection;
                m_waveDragTrack = -1;
                m_waveDragMouseX = event.x;
                m_loop.beginDrag(m_playhead, m_rulerArea);
            }
            return;
        }
        if ((SDL_GetModState() & KMOD_ALT) != 0) {
            for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
                if (m_trackHeaderRects[i].w > 0 &&
                    SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
                    selectTrack(static_cast<int>(i));
                    m_shiftingTrack = true;
                    m_shiftingTrackIndex = static_cast<int>(i);
                    m_shiftStartMouseX = event.x;
                    m_shiftStartFrame = m_timeline.track(
                        m_shiftingTrackIndex).startFrame;
                    return;
                }
            }
            for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
                if (m_trackLaneRects[i].w > 0 &&
                    SDL_PointInRect(&point, &m_trackLaneRects[i])) {
                    selectTrack(static_cast<int>(i));
                    m_shiftingTrack = true;
                    m_shiftingTrackIndex = static_cast<int>(i);
                    m_shiftStartMouseX = event.x;
                    m_shiftStartFrame = m_timeline.track(
                        m_shiftingTrackIndex).startFrame;
                    return;
                }
            }
        }
        for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
            if (m_trackHeaderRects[i].w > 0 &&
                SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
                selectTrack(static_cast<int>(i));
                return;
            }
        }
        int laneIndex = -1;
        for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
            if (m_trackLaneRects[i].w > 0 &&
                SDL_PointInRect(&point, &m_trackLaneRects[i])) {
                laneIndex = static_cast<int>(i);
                break;
            }
        }
        if (laneIndex >= 0) {
            selectTrack(laneIndex);
            const SDL_Rect& clipLane =
                m_trackLaneRects[static_cast<size_t>(laneIndex)];
            const auto& clipTrack = m_timeline.track(laneIndex);
            if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                const int64_t frame =
                    clampFrame(laneFrameAtX(clipLane, event.x),
                               m_timeline.lengthFrames());
                m_player.stop();
                m_playhead = frame;
                m_timeline.clearSelection();
                m_waveDrag = WaveDragMode::LoopSelection;
                m_waveDragTrack = laneIndex;
                m_waveDragMouseX = event.x;
                m_loop.beginDrag(frame, clipLane);
                return;
            }
            if (m_clips.toolEnabled()) {
                m_player.stop();
                m_playhead = clampFrame(laneFrameAtX(clipLane, event.x),
                                        m_timeline.lengthFrames());
                m_timeline.clearSelection();
                if (!clipTrack.document.hasClips()) {
                    setStatus("Make clips first");
                    return;
                }
                const int64_t local = std::clamp<int64_t>(
                    laneFrameAtX(clipLane, event.x) - clipTrack.startFrame,
                    0, clipTrack.document.numFrames());
                const int clipIndex =
                    clipTrack.document.clipIndexAt(local);
                if (clipIndex < 0) {
                    m_clips.setSelectedClip(-1);
                    setStatus("Click inside a clip");
                    return;
                }
                m_clips.setSelectedClip(clipIndex);
                const auto& clip =
                    clipTrack.document.clips()[static_cast<size_t>(clipIndex)];
                const int64_t frames = vwave::clipOutputFrames(clip);
                const int64_t startGlobal =
                    clipTrack.startFrame + clip.timelineStart;
                const int64_t endGlobal = startGlobal + frames;
                const int endX = laneXForFrame(clipLane, endGlobal);
                m_waveDragTrack = laneIndex;
                m_waveDragMouseX = event.x;
                m_waveDragOriginalStart = clip.timelineStart;
                m_waveDragOriginalEnd = clip.timelineStart + frames;
                m_clipDragOriginalFrames = frames;
                m_waveDragPreviewFrame = startGlobal;
                if (std::abs(event.x - endX) <= 8) {
                    m_waveDrag = WaveDragMode::ClipStretch;
                    m_waveDragPreviewFrame = endGlobal;
                } else {
                    m_waveDrag = WaveDragMode::ClipMove;
                }
                setStatus("Selected " +
                          (clip.name.empty()
                               ? "clip " + std::to_string(clipIndex + 1)
                               : clip.name));
                return;
            }
            const SDL_Rect selection = selectionScreenRect();
            if (selection.w > 0) {
                SDL_Rect repeatHandle{selection.x + selection.w - 6,
                                      selection.y + selection.h - 12, 12, 12};
                if (SDL_PointInRect(&point, &repeatHandle)) {
                    m_waveDrag = WaveDragMode::SelectionRepeat;
                    m_waveDragTrack = m_timeline.activeTrack();
                    m_waveDragMouseX = event.x;
                    m_waveDragPreviewFrame = m_timeline.selection().end;
                    m_repeatDragCount = 0;
                    return;
                }
                SDL_Rect startHandle{selection.x - 4, selection.y, 9,
                                     selection.h};
                SDL_Rect endHandle{selection.x + selection.w - 5, selection.y,
                                   9, selection.h};
                if (SDL_PointInRect(&point, &startHandle)) {
                    m_waveDrag = WaveDragMode::SelectionStart;
                    m_waveDragFixedFrame = m_timeline.selection().end;
                    m_waveDragMouseX = event.x;
                    return;
                }
                if (SDL_PointInRect(&point, &endHandle)) {
                    m_waveDrag = WaveDragMode::SelectionEnd;
                    m_waveDragFixedFrame = m_timeline.selection().start;
                    m_waveDragMouseX = event.x;
                    return;
                }
            }

            const SDL_Rect& lane =
                m_trackLaneRects[static_cast<size_t>(laneIndex)];
            const auto& track = m_timeline.track(laneIndex);
            const int64_t frames = track.document.numFrames();
            if (frames > 0) {
                const int startX = laneXForFrame(lane, track.startFrame);
                const int endX =
                    laneXForFrame(lane, track.startFrame + frames);
                SDL_Rect stretchHandle{endX - 13, lane.y + 2, 11, 11};
                SDL_Rect trimStart{startX - 4, lane.y + 2, 8, 14};
                SDL_Rect trimEnd{endX - 4, lane.y + lane.h - 16, 8, 14};
                if (endX >= lane.x && endX <= lane.x + lane.w &&
                    SDL_PointInRect(&point, &stretchHandle)) {
                    m_waveDrag = WaveDragMode::TrackStretch;
                    m_waveDragTrack = laneIndex;
                    m_waveDragMouseX = event.x;
                    m_waveDragOriginalStart = track.startFrame;
                    m_waveDragOriginalEnd = track.startFrame + frames;
                    m_waveDragPreviewFrame = m_waveDragOriginalEnd;
                    return;
                }
                if (startX >= lane.x && startX <= lane.x + lane.w &&
                    SDL_PointInRect(&point, &trimStart)) {
                    m_waveDrag = WaveDragMode::TrackTrimStart;
                    m_waveDragTrack = laneIndex;
                    m_waveDragMouseX = event.x;
                    m_waveDragOriginalStart = track.startFrame;
                    m_waveDragOriginalEnd = track.startFrame + frames;
                    m_waveDragPreviewFrame = m_waveDragOriginalStart;
                    return;
                }
                if (endX >= lane.x && endX <= lane.x + lane.w &&
                    SDL_PointInRect(&point, &trimEnd)) {
                    m_waveDrag = WaveDragMode::TrackTrimEnd;
                    m_waveDragTrack = laneIndex;
                    m_waveDragMouseX = event.x;
                    m_waveDragOriginalStart = track.startFrame;
                    m_waveDragOriginalEnd = track.startFrame + frames;
                    m_waveDragPreviewFrame = m_waveDragOriginalEnd;
                    return;
                }
            }

            m_selectionAnchor = laneFrameAtX(lane, event.x);
            m_timeline.setSelection(m_selectionAnchor, m_selectionAnchor);
            m_clips.setSelectedClip(-1);
            m_waveDrag = WaveDragMode::NewSelection;
            m_waveDragTrack = laneIndex;
            m_waveDragMouseX = event.x;
            m_clickCandidate = true;
            m_clickFrame = m_selectionAnchor;
            m_clickMouseX = event.x;
            return;
        }
        if (SDL_PointInRect(&point, &m_overviewArea)) {
            const double fraction =
                static_cast<double>(event.x - m_overviewArea.x) /
                std::max(1, m_overviewArea.w);
            m_view.startFrame =
                fraction * m_timeline.lengthFrames() -
                m_view.visibleFrames / 2.0;
            m_view.clamp(m_timeline.lengthFrames());
            m_player.stop();
            m_playhead = clampFrame(
                static_cast<int64_t>(std::llround(
                    fraction * static_cast<double>(
                                   m_timeline.lengthFrames()))),
                m_timeline.lengthFrames());
            m_panningOverview = true;
            m_lastMouseX = event.x;
            return;
        }
        if (SDL_PointInRect(&point, &m_waveArea)) {
            m_selectionAnchor =
                m_view.frameAt(event.x - m_waveArea.x, m_waveArea.w,
                               m_timeline.lengthFrames());
            m_timeline.setSelection(m_selectionAnchor, m_selectionAnchor);
            m_clips.setSelectedClip(-1);
            m_waveDrag = WaveDragMode::NewSelection;
            m_waveDragTrack = m_timeline.activeTrack();
            m_waveDragMouseX = event.x;
            m_clickCandidate = true;
            m_clickFrame = m_selectionAnchor;
            m_clickMouseX = event.x;
        }
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        if (m_spectrum3D) return;
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_spectrumArea)) {
            m_spectrumClickCandidate = false;
            const int64_t frame =
                laneFrameAtX(m_spectrumArea, event.x);
            if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                m_player.stop();
                m_playhead = clampFrame(frame, m_timeline.lengthFrames());
                m_timeline.clearSelection();
                m_spectralController.clear();
                m_waveDrag = WaveDragMode::LoopSelection;
                m_waveDragTrack = -1;
                m_waveDragMouseX = event.x;
                m_loop.beginDrag(m_playhead, m_spectrumArea);
                return;
            }
            m_spectrumClickCandidate = true;
            m_spectrumClickX = event.x;
            m_spectrumClickY = event.y;
            m_spectrumClickFrame = frame;
            m_draggingSpectrum = true;
            m_spectrumStartX = event.x;
            m_spectrumStartY = event.y;
            m_spectralController.clear();
        }
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_frameStripArea) &&
            m_wavetableController.valid()) {
            const int cellWidth = std::max(
                2, m_frameStripArea.w / std::max(1, m_wavetableController.table().frameCount()));
            const int index = (event.x - m_frameStripArea.x) / cellWidth;
            m_wavetableController.view().selectedFrame = index;
            m_wavetableController.view().clamp(m_wavetableController.table().frameCount());
        } else if (SDL_PointInRect(&point, &m_wavetableArea) &&
                   m_wavetableController.valid()) {
            m_drawingWavetable = true;
            m_lastWavetableSample = -1;
        }
    } else if (m_tab == vwave::EditorTab::Oscilloscope) {
        if (event.button != SDL_BUTTON_LEFT ||
            m_scopeController.shape() != 0)
            return;
        SDL_Point point{event.x, event.y};
        if (!SDL_PointInRect(&point, &m_scopeArea) || m_scopeArea.w <= 0 ||
            m_scopeArea.h <= 0)
            return;
        const double x =
            (static_cast<double>(event.x - m_scopeArea.x) /
             static_cast<double>(m_scopeArea.w)) *
                2.0 -
            1.0;
        const double y =
            1.0 -
            (static_cast<double>(event.y - m_scopeArea.y) /
             static_cast<double>(m_scopeArea.h)) *
                2.0;
        const vwave::ScopePoint candidate{std::clamp(x, -1.0, 1.0),
                                          std::clamp(y, -1.0, 1.0)};
        auto& path = m_scopeController.path();
        if (path.points.size() >= 3) {
            const auto& first = path.points.front();
            if (std::hypot(candidate.x - first.x, candidate.y - first.y) <
                0.06) {
                path.closed = true;
                setStatus("Scope path closed");
                return;
            }
        }
        path.name = "Path";
        path.points.push_back(candidate);
        path.closed = false;
        setStatus("Scope point " +
                  std::to_string(path.points.size()) +
                  " added");
    }
}

void App::handleMouseUp(const SDL_MouseButtonEvent& event) {
    if (event.button == SDL_BUTTON_MIDDLE) m_panning = false;
    m_panningOverview = false;
    m_drawingWavetable = false;
    m_lastWavetableSample = -1;
    m_sliderDragging = false;
    if (m_waveDrag != WaveDragMode::None) {
        const WaveDragMode drag = m_waveDrag;
        const int index = m_waveDragTrack;
        const int64_t preview = m_waveDragPreviewFrame;
        const int repeatCount = m_repeatDragCount;
        const int64_t originalStart = m_waveDragOriginalStart;
        const int64_t originalEnd = m_waveDragOriginalEnd;
        const int64_t clipOriginalFrames = m_clipDragOriginalFrames;
        m_waveDrag = WaveDragMode::None;
        m_waveDragTrack = -1;
        m_repeatDragCount = 0;
        m_clipDragOriginalFrames = 0;
        if (drag == WaveDragMode::TrackTrimStart ||
            drag == WaveDragMode::TrackTrimEnd) {
            if (index >= 0 && index < m_timeline.trackCount() &&
                preview != (drag == WaveDragMode::TrackTrimStart
                                ? originalStart
                                : originalEnd)) {
                const vwave::Range range =
                    drag == WaveDragMode::TrackTrimStart
                        ? vwave::Range{preview, originalEnd}
                        : vwave::Range{originalStart, preview};
                m_player.stop();
                std::string error;
                if (m_timeline.cropTrack(index, range, &error)) {
                    markEdited();
                    setStatus("Trimmed track");
                } else {
                    setStatus(error.empty() ? "Trim failed" : error, 6000);
                }
            }
        } else if (drag == WaveDragMode::TrackStretch) {
            if (index >= 0 && index < m_timeline.trackCount() &&
                originalEnd > originalStart) {
                const double ratio =
                    static_cast<double>(preview - originalStart) /
                    static_cast<double>(originalEnd - originalStart);
                if (std::isfinite(ratio) && ratio > 0.0 &&
                    std::abs(ratio - 1.0) > 1e-3) {
                    m_player.stop();
                    std::string error;
                    if (m_timeline.track(index).document.timeStretchTrack(
                            ratio, selectedStretchAlgorithm(), &error)) {
                        markEdited();
                        setStatus("Stretched track to " +
                                  formatRatio(ratio));
                    } else {
                        setStatus(error.empty() ? "Stretch failed" : error,
                                  6000);
                    }
                }
            }
        } else if (drag == WaveDragMode::ClipMove) {
            if (index >= 0 && index < m_timeline.trackCount() &&
                m_clips.selectedClip() >= 0) {
                auto& track = m_timeline.track(index);
                const int64_t localStart =
                    std::max<int64_t>(0, preview - track.startFrame);
                if (localStart != originalStart) {
                    m_player.stop();
                    std::string error;
                    if (track.document.moveClip(
                            static_cast<size_t>(m_clips.selectedClip()),
                            localStart,
                            &error)) {
                        markEdited();
                        setStatus("Moved clip");
                    } else {
                        setStatus(error.empty() ? "Move failed" : error, 6000);
                    }
                }
            }
        } else if (drag == WaveDragMode::ClipStretch) {
            if (index >= 0 && index < m_timeline.trackCount() &&
                m_clips.selectedClip() >= 0 && clipOriginalFrames > 0) {
                auto& track = m_timeline.track(index);
                const int64_t clipStartGlobal =
                    track.startFrame + originalStart;
                const int64_t newFrames = preview - clipStartGlobal;
                const double ratio =
                    static_cast<double>(newFrames) /
                    static_cast<double>(clipOriginalFrames);
                if (std::isfinite(ratio) && ratio > 0.0 &&
                    std::abs(ratio - 1.0) > 1e-3) {
                    m_player.stop();
                    std::string error;
                    if (track.document.stretchClip(
                            static_cast<size_t>(m_clips.selectedClip()), ratio,
                            &error)) {
                        markEdited();
                        setStatus("Stretched clip");
                    } else {
                        setStatus(error.empty() ? "Stretch failed" : error,
                                  6000);
                    }
                }
            }
        } else if (drag == WaveDragMode::SelectionRepeat) {
            if (repeatCount > 0) repeatActiveSelection(repeatCount);
        } else if (drag == WaveDragMode::LoopSelection) {
            m_loop.finishDrag(m_timeline.lengthFrames());
            if (!m_loop.active(m_timeline.lengthFrames())) {
                clearLoop();
            } else {
                setStatus("Loop " +
                          formatSeconds(static_cast<double>(m_loop.start()) /
                                        std::max(1, m_timeline.sampleRate())) +
                          " - " +
                          formatSeconds(static_cast<double>(m_loop.end()) /
                                        std::max(1, m_timeline.sampleRate())));
            }
        } else if (drag == WaveDragMode::NewSelection &&
                   m_clickCandidate) {
            m_player.stop();
            m_playhead = clampFrame(m_clickFrame,
                                    m_timeline.lengthFrames());
            m_timeline.clearSelection();
            setStatus("Playhead " +
                      formatSeconds(static_cast<double>(m_playhead) /
                                    std::max(1, m_timeline.sampleRate())));
        }
        m_clickCandidate = false;
    }
    if (m_shiftingTrack) {
        m_shiftingTrack = false;
        m_shiftingTrackIndex = -1;
        clearAnalysis();
        m_view.clamp(m_timeline.lengthFrames());
        m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
        setStatus("Shifted track");
    }
    if (m_draggingSpectrum) {
        m_draggingSpectrum = false;
        if (m_spectrumClickCandidate) {
            m_player.stop();
            m_playhead = clampFrame(m_spectrumClickFrame,
                                    m_timeline.lengthFrames());
            m_timeline.clearSelection();
            m_spectralController.clear();
            setStatus("Playhead " +
                      formatSeconds(static_cast<double>(m_playhead) /
                                    std::max(1, m_timeline.sampleRate())));
        } else if (m_spectralController.region().frames.length() < 2 ||
                   m_spectralController.region().highHz <= m_spectralController.region().lowHz) {
            m_spectralController.region().active = false;
        } else {
            m_spectralLow.setValue(m_spectralController.region().lowHz);
            m_spectralHigh.setValue(m_spectralController.region().highHz);
        }
        m_spectrumClickCandidate = false;
    }
}

void App::handleMouseMotion(const SDL_MouseMotionEvent& event) {
    if (m_sliderDragging) {
        setSliderFromX(m_activeSlider, event.x);
        return;
    }
    if (m_shiftingTrack && m_shiftingTrackIndex >= 0 &&
        m_shiftingTrackIndex < m_timeline.trackCount()) {
        const double framesPerPixel =
            m_view.samplesPerPixel(std::max(1, m_waveArea.w));
        int64_t newStart = m_shiftStartFrame + static_cast<int64_t>(
            std::llround(static_cast<double>(event.x - m_shiftStartMouseX) *
                         framesPerPixel));
        newStart = std::max<int64_t>(0, newStart);
        m_timeline.setTrackStartFrame(m_shiftingTrackIndex, newStart);
        m_view.clamp(m_timeline.lengthFrames());
        return;
    }
    if (m_tab == vwave::EditorTab::Spectrum && m_spectrum3D) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_spectrumArea)) {
            m_spectrum3DHoverX = event.x;
            m_spectrum3DHoverY = event.y;
        } else {
            m_spectrum3DHoverX = -1;
            m_spectrum3DHoverY = -1;
        }
    }
    if (m_waveDrag != WaveDragMode::None) {
        const SDL_Rect& lane =
            m_waveDragTrack >= 0 &&
                    m_waveDragTrack < static_cast<int>(m_trackLaneRects.size()) &&
                    m_trackLaneRects[static_cast<size_t>(m_waveDragTrack)].w > 0
                ? m_trackLaneRects[static_cast<size_t>(m_waveDragTrack)]
                : m_waveArea;
        const int64_t frame = laneFrameAtX(lane, event.x);
        const int64_t looseFrame = std::max<int64_t>(
            0, static_cast<int64_t>(std::llround(
                   m_view.startFrame +
                   static_cast<double>(event.x - lane.x) *
                       m_view.samplesPerPixel(std::max(1, lane.w)))));
        switch (m_waveDrag) {
        case WaveDragMode::NewSelection:
            if (m_clickCandidate &&
                std::abs(event.x - m_clickMouseX) > 2)
                m_clickCandidate = false;
            m_timeline.setSelection(std::min(m_selectionAnchor, frame),
                                    std::max(m_selectionAnchor, frame));
            break;
        case WaveDragMode::LoopSelection: {
            const int64_t loopFrame =
                m_loop.dragArea().w > 0
                    ? laneFrameAtX(m_loop.dragArea(), event.x)
                    : frame;
            m_loop.updateDrag(loopFrame);
            m_clickCandidate = false;
            break;
        }
        case WaveDragMode::SelectionStart:
            m_timeline.setSelection(
                std::min(frame, m_waveDragFixedFrame),
                std::max(frame, m_waveDragFixedFrame));
            break;
        case WaveDragMode::SelectionEnd:
            m_timeline.setSelection(
                std::min(frame, m_waveDragFixedFrame),
                std::max(frame, m_waveDragFixedFrame));
            break;
        case WaveDragMode::TrackTrimStart:
            m_waveDragPreviewFrame =
                std::clamp<int64_t>(frame, m_waveDragOriginalStart,
                                    m_waveDragOriginalEnd - 1);
            break;
        case WaveDragMode::TrackTrimEnd:
            m_waveDragPreviewFrame =
                std::clamp<int64_t>(frame, m_waveDragOriginalStart + 1,
                                    m_waveDragOriginalEnd);
            break;
        case WaveDragMode::TrackStretch: {
            const int64_t originalLength =
                m_waveDragOriginalEnd - m_waveDragOriginalStart;
            m_waveDragPreviewFrame = std::clamp<int64_t>(
                looseFrame, m_waveDragOriginalStart + 1,
                m_waveDragOriginalStart + originalLength * 20);
            break;
        }
        case WaveDragMode::SelectionRepeat: {
            const auto& selection = m_timeline.selection();
            if (selection.active && selection.length() > 0) {
                const double repeats =
                    static_cast<double>(looseFrame - selection.end) /
                    static_cast<double>(selection.length());
                m_repeatDragCount = std::clamp(
                    static_cast<int>(std::floor(repeats)), 0, 64);
                m_waveDragPreviewFrame = selection.end + m_repeatDragCount *
                                                          selection.length();
            }
            break;
        }
        case WaveDragMode::ClipMove: {
            if (m_waveDragTrack >= 0 &&
                m_waveDragTrack < m_timeline.trackCount()) {
                const auto& track =
                    m_timeline.track(m_waveDragTrack);
                const double framesPerPixel =
                    m_view.samplesPerPixel(std::max(1, lane.w));
                const int64_t delta = static_cast<int64_t>(std::llround(
                    static_cast<double>(event.x - m_waveDragMouseX) *
                    framesPerPixel));
                const int64_t localStart =
                    std::max<int64_t>(0, m_waveDragOriginalStart + delta);
                m_waveDragPreviewFrame =
                    track.startFrame + localStart;
            }
            break;
        }
        case WaveDragMode::ClipStretch: {
            if (m_waveDragTrack >= 0 &&
                m_waveDragTrack < m_timeline.trackCount() &&
                m_clipDragOriginalFrames > 0) {
                const auto& track =
                    m_timeline.track(m_waveDragTrack);
                const double framesPerPixel =
                    m_view.samplesPerPixel(std::max(1, lane.w));
                const int64_t delta = static_cast<int64_t>(std::llround(
                    static_cast<double>(event.x - m_waveDragMouseX) *
                    framesPerPixel));
                const int64_t maximum =
                    std::max<int64_t>(1, m_clipDragOriginalFrames * 20);
                const int64_t frames = std::clamp<int64_t>(
                    m_clipDragOriginalFrames + delta, 1, maximum);
                m_waveDragPreviewFrame =
                    track.startFrame + m_waveDragOriginalStart + frames;
            }
            break;
        }
        case WaveDragMode::None:
            break;
        }
        return;
    }
    if (m_panning) {
        const int delta = event.x - m_lastMouseX;
        m_view.pan(-delta, m_waveArea.w, m_timeline.lengthFrames());
        m_lastMouseX = event.x;
    }
    if (m_panningOverview) {
        const double fraction =
            static_cast<double>(event.x - m_overviewArea.x) /
            std::max(1, m_overviewArea.w);
        m_view.startFrame = fraction * m_timeline.lengthFrames() -
                            m_view.visibleFrames / 2.0;
        m_view.clamp(m_timeline.lengthFrames());
        m_lastMouseX = event.x;
    }
    if (m_draggingSpectrum) {
        if (m_spectrumClickCandidate &&
            (std::abs(event.x - m_spectrumClickX) > 2 ||
             std::abs(event.y - m_spectrumClickY) > 2)) {
            m_spectrumClickCandidate = false;
        }
        const int x0 = std::clamp(m_spectrumStartX, m_spectrumArea.x,
                                  m_spectrumArea.x + m_spectrumArea.w);
        const int x1 = std::clamp(event.x, m_spectrumArea.x,
                                  m_spectrumArea.x + m_spectrumArea.w);
        const int y0 = std::clamp(m_spectrumStartY, m_spectrumArea.y,
                                  m_spectrumArea.y + m_spectrumArea.h);
        const int y1 = std::clamp(event.y, m_spectrumArea.y,
                                  m_spectrumArea.y + m_spectrumArea.h);
        if (std::abs(x1 - x0) >= 2 && std::abs(y1 - y0) >= 2 && hasTracks()) {
            const auto& track = m_timeline.track(m_timeline.activeTrack());
            const int64_t trackFrames = track.document.numFrames();
            auto timelineFrame = [this](int x) {
                const double fraction =
                    static_cast<double>(x - m_spectrumArea.x) /
                    std::max(1, m_spectrumArea.w);
                return static_cast<int64_t>(std::llround(
                    m_view.startFrame +
                    fraction * m_view.visibleFrames));
            };
            const int64_t local0 = std::clamp<int64_t>(
                timelineFrame(std::min(x0, x1)) - track.startFrame, 0,
                trackFrames);
            const int64_t local1 = std::clamp<int64_t>(
                timelineFrame(std::max(x0, x1)) - track.startFrame, 0,
                trackFrames);
            const double hz0 = m_spectralView.frequencyAtY(
                std::min(y0, y1) - m_spectrumArea.y, m_spectrumArea.h);
            const double hz1 = m_spectralView.frequencyAtY(
                std::max(y0, y1) - m_spectrumArea.y, m_spectrumArea.h);
            m_spectralController.region().frames = {std::min(local0, local1),
                                       std::max(local0, local1)};
            m_spectralController.region().lowHz = std::min(hz0, hz1);
            m_spectralController.region().highHz = std::max(hz0, hz1);
            m_spectralController.region().active =
                m_spectralController.region().frames.length() > 0;
        }
    }
    if (m_drawingWavetable && m_wavetableController.valid()) {
        const int64_t index = static_cast<int64_t>(
            static_cast<double>(event.x - m_wavetableArea.x) /
            std::max(1, m_wavetableArea.w) *
            (m_wavetableController.table().frameSize() - 1));
        const float value = std::clamp(
            1.0f - 2.0f * static_cast<float>(
                              event.y - m_wavetableArea.y) /
                              std::max(1, m_wavetableArea.h),
            -1.0f, 1.0f);
        auto& frame = m_wavetableController.table().frame(m_wavetableController.view().selectedFrame);
        const int clampedIndex = std::clamp(
            static_cast<int>(index), 0,
            static_cast<int>(frame.size()) - 1);
        if (m_lastWavetableSample >= 0 &&
            clampedIndex != m_lastWavetableSample) {
            const int start = std::min(m_lastWavetableSample, clampedIndex);
            const int end = std::max(m_lastWavetableSample, clampedIndex);
            const float startValue = frame[static_cast<size_t>(start)];
            for (int i = start; i <= end; ++i) {
                const float mix = static_cast<float>(i - start) /
                                  std::max(1, end - start);
                frame[static_cast<size_t>(i)] =
                    startValue + (value - startValue) * mix;
            }
        } else {
            frame[static_cast<size_t>(clampedIndex)] = value;
        }
        m_lastWavetableSample = clampedIndex;
    }
}

void App::handleWheel(const SDL_MouseWheelEvent& event) {
    if (m_dialog.isOpen()) return;
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    SDL_Point wheelPoint{mouseX, mouseY};
    if (SDL_PointInRect(&wheelPoint, &m_sidebarRect)) {
        const int maximum =
            std::max(0, m_sidebarContentHeight - m_sidebarRect.h);
        m_sidebarScroll = std::clamp(
            m_sidebarScroll - static_cast<double>(event.y) * 48.0, 0.0,
            static_cast<double>(maximum));
        return;
    }
    if (!SDL_PointInRect(&wheelPoint, &m_contentRect)) return;
    if (m_tab == vwave::EditorTab::Mastering ||
        m_tab == vwave::EditorTab::Image ||
        m_tab == vwave::EditorTab::Oscilloscope)
        return;
    if (m_tab == vwave::EditorTab::Spectrum) {
        if (m_spectrum3D) return;
        if (!SDL_PointInRect(&wheelPoint, &m_spectrumArea)) return;
        if ((SDL_GetModState() & KMOD_CTRL) != 0) {
            const double center = m_spectralView.frequencyAtY(
                mouseY - m_spectrumArea.y, m_spectrumArea.h);
            const double maxHz = std::min(
                20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
            const double factor = event.y > 0 ? 2.0 : 0.5;
            m_spectralView.zoom(factor, center, 20.0, maxHz);
            m_spectrogramDirty = true;
            return;
        }
        if (event.y == 0) return;
        const double factor = event.y > 0 ? 1.25 : 0.8;
        m_view.zoomAt(factor, mouseX - m_spectrumArea.x, m_spectrumArea.w,
                      m_timeline.lengthFrames());
        return;
    }
    if (m_tab == vwave::EditorTab::Waveform) {
        if (mouseX < m_tracksArea.x) {
            const int maximum =
                std::max(0, m_trackContentHeight - m_tracksArea.h);
            m_trackScroll = std::clamp(
                m_trackScroll - static_cast<double>(event.y) * 40.0, 0.0,
                static_cast<double>(maximum));
            return;
        }
        if (event.x != 0) {
            m_view.pan(static_cast<double>(event.x) * 30.0, m_waveArea.w,
                       m_timeline.lengthFrames());
            return;
        }
        if (event.y == 0) return;
        const double factor = event.y > 0 ? 1.25 : 0.8;
        m_view.zoomAt(factor, mouseX - m_waveArea.x, m_waveArea.w,
                      m_timeline.lengthFrames());
    }
}
