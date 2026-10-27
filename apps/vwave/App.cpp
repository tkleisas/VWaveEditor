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

App::App() {
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
    m_recorder.stop();
    m_player.close();
    if (m_spectrogramTexture) SDL_DestroyTexture(m_spectrogramTexture);
    if (m_imageTexture) SDL_DestroyTexture(m_imageTexture);
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

    m_settingsPath = defaultSettingsPath();
    loadSettings();
    m_player.setOutputDevice(m_settings.outputDevice);
    if (!m_player.open() && !m_settings.outputDevice.empty()) {
        m_settings.outputDevice.clear();
        m_player.setOutputDevice("");
        m_player.open();
        saveSettings();
    }
    m_view.fit(1);
    updateWindowTitle();
    setStatus("Drop audio/image or press Ctrl+O", 6000);
    if (!initialPath.empty()) loadFile(initialPath);
    return true;
}

void App::loadSettings() {
    std::string error;
    if (!vwave::AppSettings::load(m_settingsPath, m_settings, &error)) {
        m_settings = {};
    }
    applyPalette(m_settings.theme);
}

void App::saveSettings() {
    try {
        const std::filesystem::path path(m_settingsPath);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
    } catch (...) {
    }
    std::string error;
    if (!m_settings.save(m_settingsPath, &error) && !error.empty())
        setStatus("Settings save failed: " + error, 6000);
}

void App::refreshAudioDevices() {
    m_outputDevices.clear();
    m_inputDevices.clear();
    m_outputDevices.push_back("");
    m_inputDevices.push_back("");
    const int outputCount = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < outputCount; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name && std::find(m_outputDevices.begin(), m_outputDevices.end(),
                              name) == m_outputDevices.end())
            m_outputDevices.emplace_back(name);
    }
    const int inputCount = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < inputCount; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (name && std::find(m_inputDevices.begin(), m_inputDevices.end(),
                              name) == m_inputDevices.end())
            m_inputDevices.emplace_back(name);
    }
    m_outputDeviceIndex = 0;
    m_inputDeviceIndex = 0;
    for (size_t i = 0; i < m_outputDevices.size(); ++i) {
        if (m_outputDevices[i] == m_settings.outputDevice) {
            m_outputDeviceIndex = static_cast<int>(i);
            break;
        }
    }
    for (size_t i = 0; i < m_inputDevices.size(); ++i) {
        if (m_inputDevices[i] == m_settings.inputDevice) {
            m_inputDeviceIndex = static_cast<int>(i);
            break;
        }
    }
}

void App::openSettings() {
    m_openMenu = -1;
    refreshAudioDevices();
    m_settingsOpen = true;
}

void App::closeSettings() { m_settingsOpen = false; }

void App::cycleOutputDevice(int direction) {
    if (m_outputDevices.empty()) return;
    const int count = static_cast<int>(m_outputDevices.size());
    m_outputDeviceIndex =
        (m_outputDeviceIndex + direction + count) % count;
    m_settings.outputDevice = m_outputDevices[static_cast<size_t>(
        m_outputDeviceIndex)];
    saveSettings();
    m_player.stop();
    m_player.close();
    m_player.setOutputDevice(m_settings.outputDevice);
    if (!m_player.open()) {
        m_settings.outputDevice.clear();
        m_outputDeviceIndex = 0;
        saveSettings();
        m_player.setOutputDevice("");
        if (!m_player.open())
            setStatus("Cannot open any audio output device");
        else
            setStatus("Output device unavailable; using System Default");
    } else {
        setStatus("Output: " + (m_settings.outputDevice.empty()
                                    ? std::string("System Default")
                                    : m_settings.outputDevice));
    }
}

void App::cycleInputDevice(int direction) {
    if (m_inputDevices.empty()) return;
    const int count = static_cast<int>(m_inputDevices.size());
    m_inputDeviceIndex = (m_inputDeviceIndex + direction + count) % count;
    m_settings.inputDevice = m_inputDevices[static_cast<size_t>(
        m_inputDeviceIndex)];
    saveSettings();
    setStatus("Input: " + (m_settings.inputDevice.empty()
                               ? std::string("System Default")
                               : m_settings.inputDevice));
}

void App::setTheme(std::string theme) {
    if (theme != "dark" && theme != "light") return;
    m_settings.theme = std::move(theme);
    applyPalette(m_settings.theme);
    saveSettings();
    setStatus("Theme: " + m_settings.theme);
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
    if (m_settingsOpen) drawSettingsDialog();
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

void App::syncTrackControls() {
    m_mastering = {};
    const size_t count = static_cast<size_t>(m_timeline.trackCount());
    m_trackControls.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& track = m_timeline.track(static_cast<int>(i));
        m_trackControls[i].gain = {-60.0, 12.0, 0.0, false, 0.5};
        m_trackControls[i].gain.setValue(track.gainDb);
        m_trackControls[i].pan = {-1.0, 1.0, 0.0, false, 0.05};
        m_trackControls[i].pan.setValue(track.pan);
    }
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

void App::selectTrack(int index) {
    if (index < 0 || index >= m_timeline.trackCount()) return;
    if (m_timeline.activeTrack() == index) return;
    m_timeline.setActiveTrack(index);
    m_selectedClip = -1;
    if (!activeDocument().hasClips()) m_clipTool = false;
    clearAnalysis();
    m_wavetable = {};
    m_frameWeights.clear();
    m_wavetableView.selectedFrame = 0;
    m_view.clamp(m_timeline.lengthFrames());
    setStatus("Track: " + m_timeline.track(index).name);
}

void App::removeTrack(int index) {
    if (index < 0 || index >= m_timeline.trackCount()) return;
    if (m_recording) stopRecording();
    m_player.stop();
    const std::string name = m_timeline.track(index).name;
    if (!m_timeline.removeTrack(index)) return;
    m_selectedClip = -1;
    if (!hasTracks() || !activeDocument().hasClips()) m_clipTool = false;
    syncTrackControls();
    clearAnalysis();
    m_wavetable = {};
    m_frameWeights.clear();
    m_wavetableView.selectedFrame = 0;
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    m_loop.clamp(m_timeline.lengthFrames());
    m_audioPath = hasTracks() ? activeDocument().sourcePath() : std::string{};
    if (m_timeline.empty()) m_projectPath.clear();
    updateWindowTitle();
    setStatus("Removed track " + name);
}

void App::cancelRenderJobs() {
    ++m_imageRequest;
    ++m_scopeRequest;
    ++m_wavetableRequest;
    if (m_imageJob.valid()) m_imageJob.cancel();
    if (m_scopeJob.valid()) m_scopeJob.cancel();
    if (m_wavetableJob.valid()) m_wavetableJob.cancel();
}

void App::addEmptyTrack() {
    m_player.stop();
    const int64_t start = clampFrame(m_playhead, m_timeline.lengthFrames());
    std::string error;
    if (!m_timeline.addEmptyTrack(
            "Track " + std::to_string(m_timeline.trackCount() + 1), start,
            &error)) {
        setStatus("Cannot add track: " + error);
        return;
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    m_selectedClip = -1;
    m_clipTool = false;
    for (int i = 0; i < m_timeline.trackCount(); ++i)
        m_timeline.setTrackArmed(i, i == m_timeline.activeTrack());
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    setStatus("Added empty track");
}

void App::toggleRecording() {
    if (m_recording) stopRecording();
    else startRecording();
}

void App::startRecording() {
    if (m_recording) return;
    m_player.stop();
    const int64_t recordStart =
        clampFrame(m_playhead, m_timeline.lengthFrames());
    int target = m_timeline.armedTrack();
    bool created = false;
    if (target < 0 || !m_timeline.track(target).empty) {
        std::string name = "Rec " + std::to_string(m_timeline.trackCount() + 1);
        if (target >= 0)
            name = m_timeline.track(target).name + " take";
        std::string error;
        if (!m_timeline.addEmptyTrack(name, recordStart, &error)) {
            setStatus("Cannot create recording track: " + error);
            return;
        }
        target = m_timeline.trackCount() - 1;
        created = true;
    }
    if (target < 0) {
        setStatus("No recording track");
        return;
    }
    m_timeline.setActiveTrack(target);
    for (int i = 0; i < m_timeline.trackCount(); ++i)
        m_timeline.setTrackArmed(i, i == target);
    m_timeline.setTrackStartFrame(target, recordStart);
    syncTrackControls();
    clearAnalysis();
    m_recordTarget = target;
    m_recordStartFrame = recordStart;
    m_recordedInterleaved.clear();
    m_recording = true;
    const char* inputDevice = m_settings.inputDevice.empty()
                                  ? nullptr
                                  : m_settings.inputDevice.c_str();
    if (!m_recorder.start(inputDevice, m_timeline.sampleRate())) {
        m_recording = false;
        setStatus("Cannot open audio input");
        if (created) removeTrack(target);
        return;
    }
    m_recordedChannels = m_recorder.channels();
    m_recordedSampleRate = m_recorder.sampleRate();
    setStatus("Recording... press Record or F9 to stop");
}

void App::updateRecording() {
    if (!m_recording) return;
    m_recorder.drain(m_recordedInterleaved);
}

void App::stopRecording() {
    if (!m_recording) return;
    m_recorder.drain(m_recordedInterleaved);
    m_recorder.stop();
    m_recording = false;
    if (m_recordedChannels <= 0 || m_recordedSampleRate <= 0) {
        m_recordedInterleaved.clear();
        setStatus("Recording stopped");
        return;
    }
    const int64_t frames = static_cast<int64_t>(
        m_recordedInterleaved.size() / static_cast<size_t>(m_recordedChannels));
    if (frames <= 0) {
        m_recordedInterleaved.clear();
        setStatus("Recording stopped - no input captured");
        return;
    }
    vwave::Audio recorded = vwave::Audio::fromInterleaved(
        m_recordedInterleaved.data(), m_recordedChannels, frames,
        m_recordedSampleRate);
    m_recordedInterleaved.clear();
    std::string error;
    int target = m_recordTarget;
    if (target < 0 || target >= m_timeline.trackCount()) {
        if (!m_timeline.addAudioTrack("Recording", recorded, m_recordStartFrame,
                                      &error)) {
            setStatus("Cannot store recording: " + error);
            return;
        }
        target = m_timeline.trackCount() - 1;
    } else if (m_timeline.track(target).empty) {
        if (!m_timeline.setTrackAudio(target, std::move(recorded),
                                      m_recordStartFrame, &error)) {
            setStatus("Cannot store recording: " + error);
            return;
        }
    } else {
        if (!m_timeline.addAudioTrack(
                m_timeline.track(target).name + " take", recorded,
                m_recordStartFrame, &error)) {
            setStatus("Cannot store recording: " + error);
            return;
        }
        target = m_timeline.trackCount() - 1;
    }
    m_timeline.setActiveTrack(target);
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = m_recordStartFrame;
    m_recordTarget = -1;
    setStatus("Recorded " +
              formatSeconds(static_cast<double>(frames) /
                            std::max(1, m_recordedSampleRate)));
}

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
    m_mastering = {};
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
    if (m_timeline.empty()) {
        setStatus("Load stems or add tracks first");
        return;
    }
    vwave::MasteringOptions options;
    options.profile = static_cast<vwave::MasteringProfile>(
        std::clamp(m_masteringProfile, 0,
                   static_cast<int>(sizeof(kMasteringProfileNames) /
                                    sizeof(kMasteringProfileNames[0])) -
                       1));
    options.bandCount =
        static_cast<int>(std::lround(m_masteringBands.value));
    options.maxCutDb = m_masteringMaxCut.value;
    options.maxBoostDb = m_masteringMaxBoost.value;
    options.smoothingBands =
        static_cast<int>(std::lround(m_masteringSmoothing.value));
    options.includeMuted = m_masteringIncludeMuted;
    std::string error;
    m_mastering =
        vwave::analyzeMastering(m_timeline, options, &error);
    if (!m_mastering.valid) {
        setStatus(error.empty() ? "Mastering analysis failed" : error, 6000);
        return;
    }
    setStatus("Analyzed " + std::to_string(m_mastering.stems.size()) +
              " stems across " + std::to_string(m_mastering.bandCount) +
              " bands");
}

void App::saveMasteringReport(const std::string& path) {
    if (!m_mastering.valid) {
        setStatus("Analyze stems first");
        return;
    }
    std::string error;
    if (!vwave::saveMasteringAnalysis(m_mastering, path, &error)) {
        setStatus(error.empty() ? "Mastering report save failed" : error,
                  6000);
        return;
    }
    setStatus("Saved mastering report " + baseName(path));
}

void App::renderMasteringStems() {
    if (!m_mastering.valid) {
        setStatus("Analyze stems first");
        return;
    }
    vwave::MasteringProcessingOptions options;
    options.fftSize = 2048;
    options.hopSize = 512;
    options.limitMix = true;
    options.ceilingDb = m_masteringCeiling.value;
    std::string error;
    const vwave::MasteringRenderResult rendered =
        vwave::renderMastering(m_timeline, m_mastering, options, &error);
    if (!rendered.valid) {
        setStatus(error.empty() ? "Mastering render failed" : error, 6000);
        return;
    }
    struct OriginalInfo {
        int64_t start = 0;
        double gainDb = 0.0;
        double pan = 0.0;
        bool solo = false;
        std::string name;
        int index = -1;
    };
    std::vector<OriginalInfo> originals;
    originals.reserve(rendered.stems.size());
    for (const auto& stem : rendered.stems) {
        const auto& original = m_timeline.track(stem.trackIndex);
        originals.push_back({original.startFrame, original.gainDb,
                             original.pan, original.solo, original.name,
                             stem.trackIndex});
    }
    m_player.stop();
    for (size_t i = 0; i < rendered.stems.size(); ++i) {
        const auto& stem = rendered.stems[i];
        const auto& info = originals[i];
        if (!m_timeline.addAudioTrack(info.name + " (mastered)", stem.audio,
                                      info.start, &error)) {
            setStatus("Cannot add mastered stem: " + error);
            return;
        }
        const int index = m_timeline.trackCount() - 1;
        m_timeline.setTrackGainDb(index, info.gainDb);
        m_timeline.setTrackPan(index, info.pan);
        m_timeline.setTrackSolo(index, info.solo);
        if (m_masteringMuteOriginals) {
            m_timeline.setTrackMuted(info.index, true);
            m_timeline.setTrackSolo(info.index, false);
        }
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    syncTrackControls();
    m_view.fit(m_timeline.lengthFrames());
    clearAnalysis();
    setTab(vwave::EditorTab::Waveform);
    updateWindowTitle();
    std::ostringstream status;
    status << "Added " << rendered.stems.size() << " mastered stems";
    if (rendered.appliedGainDb != 0.0)
        status << "  ceiling gain " << formatDb(rendered.appliedGainDb);
    setStatus(status.str());
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

void App::toggleSelectionScope() {
    m_timeline.setSelectionScope(
        m_timeline.selectionScope() == vwave::SelectionScope::AllTracks
            ? vwave::SelectionScope::ActiveTrack
            : vwave::SelectionScope::AllTracks);
    setStatus(m_timeline.selectionScope() ==
                      vwave::SelectionScope::ActiveTrack
                  ? "Selection: active track"
                  : "Selection: all tracks");
}

void App::cropActiveTrack() {
    if (m_timeline.empty()) return;
    const int index = m_timeline.activeTrack();
    if (!m_timeline.selection().active) {
        setStatus("Select a range first");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.cropTrack(index, m_timeline.selectedRange(), &error)) {
        setStatus(error.empty() ? "Crop failed" : error, 6000);
        return;
    }
    markEdited();
    m_view.clamp(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    setStatus("Cropped track " + m_timeline.track(index).name);
}

void App::shiftActiveTrack(double seconds) {
    if (m_timeline.empty() || m_timeline.sampleRate() <= 0) return;
    const int64_t delta = static_cast<int64_t>(
        std::llround(seconds * static_cast<double>(m_timeline.sampleRate())));
    std::string error;
    if (!m_timeline.shiftTrack(m_timeline.activeTrack(), delta, &error)) {
        setStatus(error.empty() ? "Shift failed" : error, 6000);
        return;
    }
    clearAnalysis();
    m_view.clamp(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    setStatus("Shifted track by " +
              formatSeconds(seconds));
}

void App::repeatActiveSelection(int count) {
    if (!hasTracks() || !activeDocument().selection().active) {
        setStatus("Select a range first");
        return;
    }
    count = std::clamp(count, 1, 64);
    runEdit([this, count](std::string* error) {
        return activeDocument().repeatSelection(count, error);
    }, "Repeated selection " + std::to_string(count) + "x");
}

void App::moveActiveSelectionToPlayhead(bool copy) {
    if (!hasTracks() || !activeDocument().selection().active) {
        setStatus("Select a range first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t destination = std::clamp<int64_t>(
        m_playhead - track.startFrame, 0, activeDocument().numFrames());
    runEdit([this, destination, copy](std::string* error) {
        return activeDocument().moveSelectionTo(destination, copy, error);
    }, copy ? "Copied selection to playhead"
            : "Moved selection to playhead");
}

void App::zeroCrossCut() {
    if (!hasTracks() || !activeDocument().selection().active) {
        setStatus("Select a range first");
        return;
    }
    const int64_t radius = std::max<int64_t>(
        1, activeDocument().sampleRate() / 100);
    const vwave::Range snapped = vwave::snapRangeToZeroCrossings(
        activeDocument().audio(), activeDocument().selectedRange(), radius);
    if (snapped.length() <= 0) {
        setStatus("Cannot snap selection");
        return;
    }
    activeDocument().setSelection(snapped.start, snapped.end);
    runEdit([this](std::string* error) {
        return activeDocument().deleteSelection(error);
    }, "Zero-cross cut");
}

void App::snapActiveSelection() {
    if (!hasTracks() || !activeDocument().selection().active) {
        setStatus("Select a range first");
        return;
    }
    const int64_t radius = std::max<int64_t>(
        1, activeDocument().sampleRate() / 100);
    const vwave::Range snapped = vwave::snapRangeToZeroCrossings(
        activeDocument().audio(), activeDocument().selectedRange(), radius);
    activeDocument().setSelection(snapped.start, snapped.end);
    syncSelectionFromActive();
    setStatus("Selection snapped to zero crossings");
}

void App::stretchActiveTrack() {
    if (!hasTracks() || activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    runEdit([this](std::string* error) {
        return activeDocument().timeStretchTrack(
            m_stretchRatio.value, selectedStretchAlgorithm(), error);
    }, "Track stretched");
}

void App::splitActiveStereoTrack() {
    if (!hasTracks()) return;
    const int index = m_timeline.activeTrack();
    if (m_timeline.track(index).document.numChannels() != 2) {
        setStatus("Active track is not stereo");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.splitStereoTrack(index, true, &error)) {
        setStatus(error.empty() ? "Split failed" : error, 6000);
        return;
    }
    m_selectedClip = -1;
    m_clipTool = false;
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    setStatus("Split stereo track into mono tracks");
}

void App::combineActiveMono(int direction) {
    if (!hasTracks()) return;
    const int active = m_timeline.activeTrack();
    const int other = active + direction;
    if (active < 0 || other < 0 || other >= m_timeline.trackCount()) {
        setStatus("No adjacent track to combine");
        return;
    }
    if (m_timeline.track(active).document.numChannels() != 1 ||
        m_timeline.track(other).document.numChannels() != 1) {
        setStatus("Both tracks must be mono");
        return;
    }
    m_player.stop();
    std::string error;
    vwave::StereoCombineOptions options;
    options.padding = m_stereoPadding;
    options.keepOriginals = true;
    if (!m_timeline.combineMonoTracks(
            direction < 0 ? other : active,
            direction < 0 ? active : other, options, &error)) {
        setStatus(error.empty() ? "Combine failed" : error, 6000);
        return;
    }
    m_selectedClip = -1;
    m_clipTool = false;
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    setStatus("Combined mono tracks with " +
              std::string(vwave::stereoPaddingName(m_stereoPadding)) +
              " padding");
}

void App::cycleStereoPadding() {
    switch (m_stereoPadding) {
    case vwave::StereoPadding::Back:
        m_stereoPadding = vwave::StereoPadding::Front;
        break;
    case vwave::StereoPadding::Front:
        m_stereoPadding = vwave::StereoPadding::Both;
        break;
    case vwave::StereoPadding::Both:
        m_stereoPadding = vwave::StereoPadding::Back;
        break;
    }
    setStatus("Stereo padding: " +
              std::string(vwave::stereoPaddingName(m_stereoPadding)));
}

void App::toggleClipTool() {
    if (!hasTracks() || !activeDocument().hasClips()) {
        setStatus("Make clips first");
        return;
    }
    m_clipTool = !m_clipTool;
    if (m_clipTool) {
        m_timeline.clearSelection();
        m_selectedClip = -1;
        m_selectedClip = currentClipIndex();
    }
    setStatus(m_clipTool ? "Clip tool enabled" : "Clip tool disabled");
}

void App::makeClips() {
    if (!hasTracks() || activeDocument().audio().empty()) {
        setStatus("Open a track first");
        return;
    }
    if (activeDocument().hasClips()) {
        setStatus("Track already has clips");
        return;
    }
    runEdit([this](std::string* error) {
        return activeDocument().createClipArrangement(error);
    }, "Created clip arrangement");
    if (activeDocument().hasClips()) {
        m_selectedClip = 0;
        m_clipTool = true;
        m_timeline.clearSelection();
    }
}

void App::flattenClips() {
    if (!hasTracks() || !activeDocument().hasClips()) {
        setStatus("Track has no clips");
        return;
    }
    runEdit([this](std::string* error) {
        return activeDocument().flattenClips(error);
    }, "Flattened clip arrangement");
    m_selectedClip = -1;
    m_clipTool = false;
}

void App::splitClipAtPlayhead() {
    if (!hasTracks() || !activeDocument().hasClips()) {
        setStatus("Make clips first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t frame = std::max<int64_t>(0, m_playhead - track.startFrame);
    runEdit([this, frame](std::string* error) {
        return activeDocument().splitClipAt(frame, error);
    }, "Split clip");
    m_selectedClip = activeDocument().clipIndexAt(frame);
}

void App::deleteSelectedClip() {
    const int index = currentClipIndex();
    if (index < 0) {
        setStatus("Select a clip first");
        return;
    }
    runEdit([this, index](std::string* error) {
        return activeDocument().deleteClip(static_cast<size_t>(index), error);
    }, "Deleted clip");
    if (m_selectedClip < 0 ||
        static_cast<size_t>(m_selectedClip) >=
            activeDocument().clips().size())
        m_selectedClip = static_cast<int>(activeDocument().clips().size()) - 1;
}

void App::repeatSelectedClip(int count) {
    const int index = currentClipIndex();
    if (index < 0) {
        setStatus("Select a clip first");
        return;
    }
    count = std::clamp(count, 1, 64);
    runEdit([this, index, count](std::string* error) {
        return activeDocument().repeatClip(static_cast<size_t>(index), count,
                                           error);
    }, "Repeated clip");
    m_selectedClip = index;
}

void App::moveSelectedClipToPlayhead() {
    const int index = currentClipIndex();
    if (index < 0) {
        setStatus("Select a clip first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t frame = std::max<int64_t>(0, m_playhead - track.startFrame);
    runEdit([this, index, frame](std::string* error) {
        return activeDocument().moveClip(static_cast<size_t>(index), frame,
                                         error);
    }, "Moved clip to playhead");
    m_selectedClip = index;
}

void App::stretchSelectedClip() {
    const int index = currentClipIndex();
    if (index < 0) {
        setStatus("Select a clip first");
        return;
    }
    runEdit([this, index](std::string* error) {
        return activeDocument().stretchClip(
            static_cast<size_t>(index), m_stretchRatio.value, error);
    }, "Stretched clip");
    m_selectedClip = index;
}

int App::currentClipIndex() const {
    if (m_timeline.empty()) return -1;
    const auto& document = activeDocument();
    const auto& clips = document.clips();
    if (clips.empty()) return -1;
    if (m_selectedClip >= 0 &&
        m_selectedClip < static_cast<int>(clips.size()))
        return m_selectedClip;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    int index = document.clipIndexAt(m_playhead - track.startFrame);
    if (index < 0 && m_timeline.selection().active) {
        index = document.clipIndexAt(m_timeline.selection().start -
                                     track.startFrame);
    }
    return index;
}

bool App::seekToTime(double seconds, std::string* error) {
    if (error) error->clear();
    if (m_timeline.empty() || m_timeline.sampleRate() <= 0) {
        if (error) *error = "No timeline audio";
        return false;
    }
    if (!std::isfinite(seconds) || seconds < 0.0) {
        if (error) *error = "Invalid time";
        return false;
    }
    m_player.stop();
    m_playhead = clampFrame(
        static_cast<int64_t>(std::llround(
            seconds * static_cast<double>(m_timeline.sampleRate()))),
        m_timeline.lengthFrames());
    const double viewEnd = m_view.startFrame + m_view.visibleFrames;
    if (m_playhead < m_view.startFrame || m_playhead > viewEnd) {
        m_view.startFrame =
            static_cast<double>(m_playhead) - m_view.visibleFrames * 0.15;
        m_view.clamp(m_timeline.lengthFrames());
    }
    setStatus("Playhead " +
              formatSeconds(static_cast<double>(m_playhead) /
                            m_timeline.sampleRate()));
    return true;
}

void App::goToStart() {
    seekToTime(0.0);
}

void App::goToEnd() {
    if (m_timeline.sampleRate() > 0) {
        seekToTime(static_cast<double>(m_timeline.lengthFrames()) /
                   static_cast<double>(m_timeline.sampleRate()));
    }
}

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
    return m_loop.active(m_timeline.lengthFrames());
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

void App::cycleImagePhaseMode() {
    m_imagePhaseMode = (m_imagePhaseMode + 1) % 5;
}

void App::cycleImageFrequencyScale() {
    m_imageFrequencyScale = (m_imageFrequencyScale + 1) % 2;
}

void App::cycleImageTimeMapping() {
    m_imageTimeMapping = (m_imageTimeMapping + 1) % 3;
}

void App::loadImage(const std::string& path) {
    vwave::ImageData image;
    std::string error;
    if (!vwave::loadImageFile(path, image, &error)) {
        setStatus(error.empty() ? "Image load failed" : error, 6000);
        return;
    }
    if (m_imageTexture) {
        SDL_DestroyTexture(m_imageTexture);
        m_imageTexture = nullptr;
    }
    m_image = std::move(image);
    m_imagePath = path;
    m_imageTexture = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
        m_image.width, m_image.height);
    if (m_imageTexture) {
        SDL_UpdateTexture(m_imageTexture, nullptr, m_image.pixels.data(),
                          m_image.width * 4);
        SDL_SetTextureBlendMode(m_imageTexture, SDL_BLENDMODE_BLEND);
    }
    setTab(vwave::EditorTab::Image);
    setStatus("Loaded image " + baseName(path));
}

void App::renderImageToTrack() {
    if (!m_image.valid()) {
        setStatus("Load an image first");
        return;
    }
    vwave::ImageSpectrogramOptions options;
    options.sampleRate =
        m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000;
    options.fftSize = kFrameSizes[m_imageFftIndex];
    options.hopSize = std::max(64, options.fftSize / 4);
    options.duration = m_imageDuration.value;
    options.minHz = m_imageMinHz.value;
    options.maxHz = m_imageMaxHz.value;
    options.frequencyScale = static_cast<vwave::ImageFrequencyScale>(
        m_imageFrequencyScale);
    options.timeMapping = static_cast<vwave::ImageTimeMapping>(
        m_imageTimeMapping);
    options.loops = static_cast<int>(std::lround(m_imageLoops.value));
    options.flipHorizontal = m_imageFlipHorizontal;
    options.flipVertical = m_imageFlipVertical;
    options.invert = m_imageInvert;
    options.gamma = m_imageGamma.value;
    options.contrast = m_imageContrast.value;
    options.threshold = m_imageThreshold.value;
    options.useAlpha = m_imageUseAlpha;
    options.phaseMode = static_cast<vwave::ImagePhaseMode>(m_imagePhaseMode);
    options.iterations =
        static_cast<int>(std::lround(m_imageIterations.value));
    options.channels = m_imageStereo ? 2 : 1;
    options.normalize = m_imageNormalize;
    options.peak = 0.9;

    const vwave::ImageSpectrogramOptions recipeOptions = options;
    const vwave::ImageData image = m_image;
    const std::string imagePath = m_imagePath;
    const int64_t start = clampFrame(m_playhead, m_timeline.lengthFrames());
    std::string name = std::filesystem::path(imagePath).stem().string();
    if (name.empty()) name = "Image";
    const uint64_t request = ++m_imageRequest;
    if (m_imageJob.valid()) m_imageJob.cancel();

    m_imageJob = m_jobs.submit(
        [this, request, image, options, recipeOptions, imagePath, start,
         name](vwave::JobControl& control) mutable {
            options.cancelled = [&control] { return control.cancelled(); };
            options.progress = [&control](double value) {
                control.setProgress(value);
            };
            std::string error;
            vwave::ImageSpectrogramResult generated;
            try {
                generated = vwave::imageToSpectrogram(image, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, generated = std::move(generated),
                 error = std::move(error), imagePath, recipeOptions, start,
                 name]() mutable {
                    if (request != m_imageRequest) return;
                    if (generated.audio.empty()) {
                        setStatus(error.empty() ? "Image render failed" : error,
                                  6000);
                        return;
                    }
                    m_player.stop();
                    std::string addError;
                    if (!m_timeline.addAudioTrack(name, generated.audio, start,
                                                  &addError)) {
                        setStatus("Cannot add image track: " + addError);
                        return;
                    }
                    const int trackIndex = m_timeline.trackCount() - 1;
                    m_timeline.track(trackIndex).recipe =
                        vwave::makeImageTrackRecipe(imagePath, recipeOptions);
                    m_timeline.setActiveTrack(trackIndex);
                    syncTrackControls();
                    m_view.fit(m_timeline.lengthFrames());
                    m_playhead = start;
                    clearAnalysis();
                    setTab(vwave::EditorTab::Waveform);
                    updateWindowTitle();
                    setStatus("Rendered image to track (" +
                              formatSeconds(generated.duration) + ")");
                });
        });
    setStatus("Rendering image in background...", 60000);
}

void App::generateScopePath() {
    if (m_scopeShape <= 0) return;
    const vwave::ScopeShape shape =
        static_cast<vwave::ScopeShape>(m_scopeShape);
    m_scopePath = makeScopeShape(
        shape, static_cast<int>(std::lround(m_scopePointCount.value)),
        m_scopeSize.value, m_scopeA.value, m_scopeB.value,
        m_scopePhase.value,
        static_cast<int>(std::lround(m_scopeTurns.value)));
}

void App::cycleScopeShape() {
    m_scopeShape = (m_scopeShape + 1) %
                   static_cast<int>(sizeof(kScopeShapeNames) /
                                    sizeof(kScopeShapeNames[0]));
    if (m_scopeShape == 0) {
        m_scopePath = {};
        m_scopePath.name = "Path";
        setStatus("Polyline mode: click the scope to add points");
    } else {
        generateScopePath();
        setStatus("Scope shape: " + std::string(kScopeShapeNames[m_scopeShape]));
    }
}

void App::cycleScopeDirection() {
    m_scopeDirection = (m_scopeDirection + 1) % 3;
}

void App::cycleScopeOversampling() {
    m_scopeOversampling = (m_scopeOversampling + 1) % 4;
}

void App::renderScopeToTrack() {
    if (!m_scopePath.valid()) {
        setStatus("Create or load a scope path first");
        return;
    }
    vwave::ScopeRenderOptions options;
    options.sampleRate =
        m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000;
    options.duration = m_scopeDuration.value;
    options.scanHz = m_scopeScanHz.value;
    options.gain = m_scopeGain.value;
    options.smoothing = m_scopeSmoothing.value;
    options.bandLimited = m_scopeBandLimited;
    options.bandLimitHz = 16000.0;
    options.oversampling = kScopeOversampling[m_scopeOversampling];
    options.direction = static_cast<vwave::ScopeDirection>(m_scopeDirection);
    options.normalize = m_scopeNormalize;
    options.peak = 0.9;

    const vwave::ScopePath path = m_scopePath;
    const int64_t start = clampFrame(m_playhead, m_timeline.lengthFrames());
    const std::string name =
        path.name.empty() ? std::string("Scope") : path.name;
    const uint64_t request = ++m_scopeRequest;
    if (m_scopeJob.valid()) m_scopeJob.cancel();

    m_scopeJob = m_jobs.submit(
        [this, request, path, options, start, name](
            vwave::JobControl& control) mutable {
            std::string error;
            vwave::Audio rendered;
            try {
                rendered = vwave::renderScopePath(path, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, rendered = std::move(rendered),
                 error = std::move(error), path, options, start, name]() mutable {
                    if (request != m_scopeRequest) return;
                    if (rendered.empty()) {
                        setStatus(error.empty() ? "Scope render failed" : error,
                                  6000);
                        return;
                    }
                    m_player.stop();
                    std::string addError;
                    if (!m_timeline.addAudioTrack(name, rendered, start,
                                                  &addError)) {
                        setStatus("Cannot add scope track: " + addError);
                        return;
                    }
                    const int trackIndex = m_timeline.trackCount() - 1;
                    m_timeline.track(trackIndex).recipe =
                        vwave::makeScopeTrackRecipe(path, options);
                    m_timeline.setActiveTrack(trackIndex);
                    syncTrackControls();
                    m_view.fit(m_timeline.lengthFrames());
                    m_playhead = start;
                    clearAnalysis();
                    setTab(vwave::EditorTab::Waveform);
                    updateWindowTitle();
                    setStatus("Rendered scope path to track (" +
                              formatSeconds(rendered.duration()) + ")");
                });
        });
    setStatus("Rendering scope in background...", 60000);
}

void App::loadScopePathFile(const std::string& path) {
    vwave::ScopePath loaded;
    std::string error;
    if (!vwave::loadScopePath(path, loaded, &error)) {
        setStatus(error.empty() ? "Scope path load failed" : error, 6000);
        return;
    }
    m_scopePath = std::move(loaded);
    m_scopeShape = 0;
    setTab(vwave::EditorTab::Oscilloscope);
    setStatus("Loaded scope path " + baseName(path));
}

void App::saveScopePathFile(const std::string& path) {
    if (!m_scopePath.valid()) {
        setStatus("Create a scope path first");
        return;
    }
    const std::string target = withExtension(path, ".vscope");
    std::string error;
    if (!vwave::saveScopePath(m_scopePath, target, &error)) {
        setStatus(error.empty() ? "Scope path save failed" : error, 6000);
        return;
    }
    setStatus("Saved scope path " + baseName(target));
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
    if (m_recording) stopRecording();
    if (m_player.isPlaying()) {
        stopPlayback();
        return;
    }
    if (m_timeline.empty()) {
        setStatus("Add or open an audio file first");
        return;
    }
    vwave::Range range;
    const bool loop = loopPlaybackActive();
    if (loop) {
        const int64_t loopEnd = std::clamp<int64_t>(
            m_loop.end(), 0, m_timeline.lengthFrames());
        const int64_t loopStart = std::clamp<int64_t>(
            m_loop.start(), 0, m_timeline.lengthFrames());
        int64_t start = clampFrame(m_playhead, m_timeline.lengthFrames());
        if (start < loopStart || start >= loopEnd) start = loopStart;
        range = {start, loopEnd};
    } else if (m_timeline.selection().active) {
        range = m_timeline.selectedRange();
    } else {
        int64_t start = clampFrame(m_playhead, m_timeline.lengthFrames());
        if (start >= m_timeline.lengthFrames()) start = 0;
        range = {start, m_timeline.lengthFrames()};
    }
    if (range.empty()) {
        setStatus("Nothing to play");
        return;
    }
    if (m_player.play(m_timeline, range.start, range.end, loop)) {
        m_playhead = range.start;
        setStatus(loop ? "Playing loop" : "Playing mix");
    } else {
        setStatus("Audio playback is unavailable");
    }
}

void App::stopPlayback() {
    m_player.stop();
    if (m_player.sourceStart() <= m_timeline.lengthFrames())
        m_playhead = m_player.sourceStart();
}

void App::updatePlayback() {
    if (!m_player.isPlaying()) return;
    m_playhead = clampFrame(m_player.sourcePosition(),
                            m_timeline.lengthFrames());
    const double viewEnd = m_view.startFrame + m_view.visibleFrames;
    if (m_playhead < m_view.startFrame || m_playhead > viewEnd) {
        m_view.startFrame =
            static_cast<double>(m_playhead) - m_view.visibleFrames * 0.15;
        m_view.clamp(m_timeline.lengthFrames());
    }
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
    m_mastering = {};
    m_phaseSpectrogramDirty = true;
    m_loop.clamp(m_timeline.lengthFrames());
    if (!hasTracks() || !activeDocument().hasClips()) {
        m_clipTool = false;
        m_selectedClip = -1;
    } else if (m_selectedClip >=
               static_cast<int>(activeDocument().clips().size())) {
        m_selectedClip =
            static_cast<int>(activeDocument().clips().size()) - 1;
    }
    syncSelectionFromActive();
    m_view.clamp(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    m_overviewDirty = true;
    m_spectrogramDirty = true;
    m_hasBeats = false;
    m_hasPitch = false;
    m_beats = {};
    m_pitch = {};
    m_spectralRegion.clear();
    updateWindowTitle();
}

void App::clearAnalysis() {
    m_mastering = {};
    m_phaseSpectrogramDirty = true;
    m_hasBeats = false;
    m_hasPitch = false;
    m_beats = {};
    m_pitch = {};
    m_overviewDirty = true;
    m_spectrogramDirty = true;
    m_spectralRegion.clear();
}

void App::analyzeBeats() {
    if (activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    setStatus("Analyzing beats...", 60000);
    m_beats = vwave::analyzeBeats(activeDocument().audio(),
                                  m_beatSensitivity.value);
    m_hasBeats = true;
    if (m_beats.bpm > 0.0) {
        std::ostringstream out;
        out << "Detected " << std::fixed << std::setprecision(1)
            << m_beats.bpm << " BPM";
        setStatus(out.str());
    } else {
        setStatus("No tempo found");
    }
}

void App::detectPitch() {
    if (activeDocument().audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    const vwave::Range range = activeDocument().selection().active
                                   ? activeDocument().selectedRange()
                                   : vwave::Range{};
    m_pitch = vwave::detectPitch(activeDocument().audio(), range);
    m_hasPitch = true;
    if (m_pitch.frequency > 0.0) {
        std::ostringstream out;
        out << "Pitch " << std::fixed << std::setprecision(2)
            << m_pitch.frequency << " Hz";
        setStatus(out.str());
    } else {
        setStatus("No stable pitch found");
    }
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
        const vwave::BeatAnalysis analysis = m_hasBeats
                                                 ? m_beats
                                                 : vwave::analyzeBeats(
                                                       activeDocument().audio(),
                                                       m_beatSensitivity.value);
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
    if (m_spectralRegion.active && !m_spectralRegion.frames.empty())
        return m_spectralRegion.frames;
    if (activeDocument().selection().active)
        return activeDocument().selectedRange();
    return {0, activeDocument().numFrames()};
}

void App::addMarkerAtPlayhead() {
    if (activeDocument().audio().empty() || !hasTracks()) return;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t localFrame =
        m_playhead - track.startFrame;
    if (localFrame < 0 || localFrame > activeDocument().numFrames()) {
        setStatus("Playhead is outside the active track");
        return;
    }
    const int64_t frame = localFrame;
    if (activeDocument().addMarker(frame, "Marker")) {
        setStatus("Marker added at " + formatSeconds(
                                       static_cast<double>(frame) /
                                       std::max(1, activeDocument().sampleRate())));
    }
}

bool App::loadFile(const std::string& path) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".vwdoc") return loadProject(path);
    if (isScopePath(path)) {
        loadScopePathFile(path);
        return m_scopePath.valid();
    }
    if (isImagePath(path)) {
        loadImage(path);
        return m_image.valid();
    }
    return loadAudio(path);
}

bool App::loadAudio(const std::string& path) {
    if (m_recording) stopRecording();
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
    m_selectedClip = -1;
    m_clipTool = false;
    m_loop.clear();
    syncTrackControls();
    m_wavetable = {};
    m_frameWeights.clear();
    m_wavetableView.selectedFrame = 0;
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
    if (m_recording) stopRecording();
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
    m_selectedClip = -1;
    m_clipTool = false;
    syncTrackControls();
    clearAnalysis();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    updateWindowTitle();
    setStatus("Added track " + name);
    return true;
}

bool App::loadProject(const std::string& path) {
    if (m_recording) stopRecording();
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
    m_selectedClip = -1;
    m_clipTool = false;
    m_loop.clear();
    m_wavetable = {};
    m_frameWeights.clear();
    m_wavetableView.selectedFrame = 0;
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
    std::string error;
    m_wavetable = vwave::extractWavetable(activeDocument().audio(), options,
                                          &error);
    if (!m_wavetable.valid()) {
        setStatus(error.empty() ? "Wavetable extraction failed" : error);
        return;
    }
    m_frameWeights.assign(static_cast<size_t>(m_wavetable.frameCount()), 1.0);
    m_freeBlend = false;
    m_wavetable.setRootNote(m_tableRootControl.value);
    m_wavetableView.selectedFrame = 0;
    syncWavetableDesignControls();
    setTab(vwave::EditorTab::Wavetable);
    setStatus("Extracted " + std::to_string(m_wavetable.frameCount()) +
              " wavetable frames");
}

void App::loadWavetable(const std::string& path) {
    std::string error;
    vwave::Wavetable loaded;
    if (!vwave::Wavetable::loadAuto(path, loaded, &error)) {
        setStatus(error.empty() ? "Wavetable load failed" : error, 6000);
        return;
    }
    m_wavetable = std::move(loaded);
    m_frameWeights.assign(static_cast<size_t>(m_wavetable.frameCount()), 1.0);
    m_freeBlend = false;
    m_wavetableView.selectedFrame = 0;
    syncWavetableDesignControls();
    setTab(vwave::EditorTab::Wavetable);
    setStatus("Loaded wavetable " + baseName(path));
}

void App::saveWavetable(const std::string& path) {
    if (!m_wavetable.valid()) {
        setStatus("No wavetable to save");
        return;
    }
    const std::string extension =
        std::filesystem::path(path).extension().string();
    std::string error;
    bool success = false;
    std::string target = path;
    if (extension == ".wav") {
        success = m_wavetable.saveWav(path, activeDocument().sampleRate(), &error);
    } else {
        target = withExtension(path, ".vwt");
        success = m_wavetable.save(target, &error);
    }
    if (!success) {
        setStatus("Wavetable save failed: " + error, 6000);
        return;
    }
    setStatus("Saved wavetable " + baseName(target));
}

void App::syncWavetableDesignControls() {
    if (!m_wavetable.valid()) return;
    if (static_cast<int>(m_frameWeights.size()) != m_wavetable.frameCount())
        m_frameWeights.assign(static_cast<size_t>(m_wavetable.frameCount()),
                              1.0);
    m_wavetableView.clamp(m_wavetable.frameCount());
    const int frame = m_wavetableView.selectedFrame;
    m_framePositionControl.setValue(m_wavetable.framePosition(frame));
    m_frameWeightControl.setValue(m_frameWeights[static_cast<size_t>(frame)]);
    m_tableRootControl.setValue(m_wavetable.rootNote());
}

void App::addWavetableFrame(bool duplicate) {
    if (!m_wavetable.valid()) return;
    const int selected = std::clamp(m_wavetableView.selectedFrame, 0,
                                    m_wavetable.frameCount() - 1);
    std::vector<float> samples =
        duplicate ? m_wavetable.frame(selected)
                  : std::vector<float>(
                        static_cast<size_t>(m_wavetable.frameSize()), 0.0f);
    double position = 1.0;
    if (m_wavetable.frameCount() > 1) {
        const double current = m_wavetable.framePosition(selected);
        const double next =
            selected + 1 < m_wavetable.frameCount()
                ? m_wavetable.framePosition(selected + 1)
                : 1.0;
        position = std::clamp((current + next) * 0.5, 0.0, 1.0);
    }
    if (!m_wavetable.addKeyframe(samples, position)) {
        setStatus("Cannot add wavetable frame");
        return;
    }
    m_frameWeights.push_back(1.0);
    m_wavetableView.selectedFrame = m_wavetable.frameCount() - 1;
    syncWavetableDesignControls();
    setStatus(duplicate ? "Duplicated wavetable frame"
                        : "Added wavetable frame");
}

void App::addHarmonicWavetableFrame() {
    if (!m_wavetable.valid()) return;
    const int frameSize = m_wavetable.frameSize();
    const int harmonics = std::clamp(
        static_cast<int>(std::lround(m_harmonicCount.value)), 1,
        std::min(128, frameSize / 2));
    std::vector<double> amplitudes(static_cast<size_t>(harmonics + 1), 0.0);
    for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
        amplitudes[static_cast<size_t>(harmonic)] =
            1.0 / static_cast<double>(harmonic);
    vwave::Wavetable generated(frameSize);
    generated.addFrame(std::vector<float>(static_cast<size_t>(frameSize), 0.0f));
    if (!generated.setFrameFromHarmonics(0, amplitudes)) {
        setStatus("Cannot generate harmonic frame");
        return;
    }
    generated.normalizeFrames(0.9f);
    const int selected = std::clamp(m_wavetableView.selectedFrame, 0,
                                    m_wavetable.frameCount() - 1);
    double position = 1.0;
    if (m_wavetable.frameCount() > 1) {
        const double current = m_wavetable.framePosition(selected);
        const double next =
            selected + 1 < m_wavetable.frameCount()
                ? m_wavetable.framePosition(selected + 1)
                : 1.0;
        position = std::clamp((current + next) * 0.5, 0.0, 1.0);
    }
    if (!m_wavetable.addKeyframe(generated.frame(0), position)) {
        setStatus("Cannot add harmonic frame");
        return;
    }
    m_frameWeights.push_back(1.0);
    m_wavetableView.selectedFrame = m_wavetable.frameCount() - 1;
    syncWavetableDesignControls();
    setStatus("Added " + std::to_string(harmonics) + "-harmonic frame");
}

void App::deleteWavetableFrame() {
    if (!m_wavetable.valid()) return;
    if (m_wavetable.frameCount() <= 1) {
        setStatus("Keep at least one wavetable frame");
        return;
    }
    const int selected = std::clamp(m_wavetableView.selectedFrame, 0,
                                    m_wavetable.frameCount() - 1);
    if (!m_wavetable.removeFrame(selected)) return;
    if (selected < static_cast<int>(m_frameWeights.size()))
        m_frameWeights.erase(m_frameWeights.begin() + selected);
    m_wavetableView.clamp(m_wavetable.frameCount());
    syncWavetableDesignControls();
    setStatus("Removed wavetable frame");
}

void App::cycleWavetableMorphMode() {
    if (!m_wavetable.valid()) return;
    const int next =
        (static_cast<int>(m_wavetable.morphMode()) + 1) % 5;
    m_wavetable.setMorphMode(static_cast<vwave::WavetableMorphMode>(next));
    syncWavetableDesignControls();
}

void App::cycleWavetableInterpolation() {
    if (!m_wavetable.valid()) return;
    const int next =
        (static_cast<int>(m_wavetable.interpolation()) + 1) % 3;
    m_wavetable.setInterpolation(
        static_cast<vwave::WavetableInterpolation>(next));
    syncWavetableDesignControls();
}

void App::cycleWavetableOversampling() {
    if (!m_wavetable.valid()) return;
    int index = 0;
    for (int i = 0;
         i < static_cast<int>(sizeof(kWavetableOversampling) /
                              sizeof(kWavetableOversampling[0]));
         ++i) {
        if (kWavetableOversampling[i] == m_wavetable.oversampling()) {
            index = i;
            break;
        }
    }
    index = (index + 1) %
            static_cast<int>(sizeof(kWavetableOversampling) /
                             sizeof(kWavetableOversampling[0]));
    m_wavetable.setOversampling(kWavetableOversampling[index]);
}

void App::renderWavetableNote() {
    if (!m_wavetable.valid()) {
        setStatus("Extract a wavetable first");
        return;
    }
    vwave::SynthNoteOptions options;
    options.sampleRate = 48000;
    options.channels = 2;
    options.rootNote = m_synthRoot.value;
    options.gain = m_synthGain.value;
    options.morph = m_synthMorph.value;
    options.unison = static_cast<int>(std::lround(m_synthUnison.value));
    if (m_freeBlend) {
        if (static_cast<int>(m_frameWeights.size()) ==
            m_wavetable.frameCount()) {
            options.blendWeights = m_frameWeights;
        }
    }
    const vwave::Wavetable table = m_wavetable;
    const float note = static_cast<float>(m_synthNote.value);
    const double duration = m_synthDuration.value;
    const uint64_t request = ++m_wavetableRequest;
    if (m_wavetableJob.valid()) m_wavetableJob.cancel();

    m_wavetableJob = m_jobs.submit(
        [this, request, table, note, duration, options](
            vwave::JobControl& control) mutable {
            std::string error;
            vwave::Audio rendered;
            try {
                rendered = vwave::renderWavetableNote(
                    table, note, duration, options);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post([this, request, rendered = std::move(rendered),
                               error = std::move(error)]() mutable {
                if (request != m_wavetableRequest) return;
                if (rendered.empty()) {
                    setStatus(error.empty() ? "Synth render failed" : error,
                              6000);
                    return;
                }
                m_player.stop();
                std::string addError;
                if (!m_timeline.addAudioTrack("Synth", rendered, 0,
                                              &addError)) {
                    setStatus("Cannot add synth track: " + addError);
                    return;
                }
                m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
                syncTrackControls();
                m_view.fit(m_timeline.lengthFrames());
                m_playhead = 0;
                m_tab = vwave::EditorTab::Waveform;
                clearAnalysis();
                updateWindowTitle();
                setStatus("Rendered wavetable note as a new track");
            });
        });
    setStatus("Rendering wavetable note...", 60000);
}

void App::applySpectralGain(double db) {
    if (activeDocument().audio().empty()) return;
    const vwave::Range range = spectralTimeRange();
    if (range.empty()) {
        setStatus("Select a time range or drag a spectral region first");
        return;
    }
    m_player.stop();
    activeDocument().setSelection(range.start, range.end);
    std::string error;
    if (!activeDocument().spectralGain(m_spectralLow.value, m_spectralHigh.value, db,
                                 &error)) {
        setStatus(error.empty() ? "Spectral edit failed" : error);
        return;
    }
    markEdited();
    setStatus(db <= -100.0 ? "Spectral region erased" : "Spectral gain applied");
}

void App::applySpectralFilter(bool lowpass) {
    if (activeDocument().audio().empty()) return;
    const vwave::Range range = spectralTimeRange();
    if (range.empty()) {
        setStatus("Select a time range or drag a spectral region first");
        return;
    }
    m_player.stop();
    activeDocument().setSelection(range.start, range.end);
    std::string error;
    if (!activeDocument().spectralFilter(lowpass ? "lowpass" : "highpass",
                                   m_spectralCutoff.value, &error)) {
        setStatus(error.empty() ? "Spectral filter failed" : error);
        return;
    }
    markEdited();
    setStatus(lowpass ? "Lowpass applied" : "Highpass applied");
}

void App::applySpectralGate() {
    if (activeDocument().audio().empty()) return;
    const vwave::Range range = spectralTimeRange();
    if (range.empty()) {
        setStatus("Select a time range or drag a spectral region first");
        return;
    }
    m_player.stop();
    activeDocument().setSelection(range.start, range.end);
    std::string error;
    if (!activeDocument().spectralGate(m_spectralGate.value, &error)) {
        setStatus(error.empty() ? "Spectral gate failed" : error);
        return;
    }
    markEdited();
    setStatus("Spectral gate applied");
}

void App::applySpectralBlur() {
    if (activeDocument().audio().empty()) return;
    const vwave::Range range = spectralTimeRange();
    if (range.empty()) {
        setStatus("Select a time range or drag a spectral region first");
        return;
    }
    m_player.stop();
    activeDocument().setSelection(range.start, range.end);
    std::string error;
    if (!activeDocument().spectralBlur(
            static_cast<int>(std::lround(m_spectralBlurTime.value)),
            static_cast<int>(std::lround(m_spectralBlurFrequency.value)),
            &error)) {
        setStatus(error.empty() ? "Spectral blur failed" : error);
        return;
    }
    markEdited();
    setStatus("Spectral blur applied");
}

void App::openDialog(DialogMode mode, const std::string& title,
                     const std::string& initialPath, bool save) {
    m_openMenu = -1;
    m_settingsOpen = false;
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
    if (m_settingsOpen) {
        if (key == SDLK_ESCAPE) closeSettings();
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
        openSettings();
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
        if (m_spectralRegion.active) {
            m_spectralRegion.clear();
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
    if (m_settingsOpen) {
        if (event.button != SDL_BUTTON_LEFT) return;
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_settingsCloseRect)) closeSettings();
        else if (SDL_PointInRect(&point, &m_settingsOutputPrev))
            cycleOutputDevice(-1);
        else if (SDL_PointInRect(&point, &m_settingsOutputNext))
            cycleOutputDevice(1);
        else if (SDL_PointInRect(&point, &m_settingsInputPrev))
            cycleInputDevice(-1);
        else if (SDL_PointInRect(&point, &m_settingsInputNext))
            cycleInputDevice(1);
        else if (SDL_PointInRect(&point, &m_settingsThemePrev))
            setTheme(m_settings.theme == "dark" ? "light" : "dark");
        else if (SDL_PointInRect(&point, &m_settingsThemeNext))
            setTheme(m_settings.theme == "dark" ? "light" : "dark");
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
        if (m_clipTool && !activeDocument().hasClips()) {
            m_clipTool = false;
            m_selectedClip = -1;
        }
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
            if (m_clipTool) {
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
                    m_selectedClip = -1;
                    setStatus("Click inside a clip");
                    return;
                }
                m_selectedClip = clipIndex;
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
            m_selectedClip = -1;
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
            m_selectedClip = -1;
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
                m_spectralRegion.clear();
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
            m_spectralRegion.clear();
        }
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_frameStripArea) &&
            m_wavetable.valid()) {
            const int cellWidth = std::max(
                2, m_frameStripArea.w / std::max(1, m_wavetable.frameCount()));
            const int index = (event.x - m_frameStripArea.x) / cellWidth;
            m_wavetableView.selectedFrame = index;
            m_wavetableView.clamp(m_wavetable.frameCount());
        } else if (SDL_PointInRect(&point, &m_wavetableArea) &&
                   m_wavetable.valid()) {
            m_drawingWavetable = true;
            m_lastWavetableSample = -1;
        }
    } else if (m_tab == vwave::EditorTab::Oscilloscope) {
        if (event.button != SDL_BUTTON_LEFT || m_scopeShape != 0) return;
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
        if (m_scopePath.points.size() >= 3) {
            const auto& first = m_scopePath.points.front();
            if (std::hypot(candidate.x - first.x, candidate.y - first.y) <
                0.06) {
                m_scopePath.closed = true;
                setStatus("Scope path closed");
                return;
            }
        }
        m_scopePath.name = "Path";
        m_scopePath.points.push_back(candidate);
        m_scopePath.closed = false;
        setStatus("Scope point " +
                  std::to_string(m_scopePath.points.size()) +
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
                m_selectedClip >= 0) {
                auto& track = m_timeline.track(index);
                const int64_t localStart =
                    std::max<int64_t>(0, preview - track.startFrame);
                if (localStart != originalStart) {
                    m_player.stop();
                    std::string error;
                    if (track.document.moveClip(
                            static_cast<size_t>(m_selectedClip), localStart,
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
                m_selectedClip >= 0 && clipOriginalFrames > 0) {
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
                            static_cast<size_t>(m_selectedClip), ratio,
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
            m_spectralRegion.clear();
            setStatus("Playhead " +
                      formatSeconds(static_cast<double>(m_playhead) /
                                    std::max(1, m_timeline.sampleRate())));
        } else if (m_spectralRegion.frames.length() < 2 ||
                   m_spectralRegion.highHz <= m_spectralRegion.lowHz) {
            m_spectralRegion.active = false;
        } else {
            m_spectralLow.setValue(m_spectralRegion.lowHz);
            m_spectralHigh.setValue(m_spectralRegion.highHz);
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
            m_spectralRegion.frames = {std::min(local0, local1),
                                       std::max(local0, local1)};
            m_spectralRegion.lowHz = std::min(hz0, hz1);
            m_spectralRegion.highHz = std::max(hz0, hz1);
            m_spectralRegion.active =
                m_spectralRegion.frames.length() > 0;
        }
    }
    if (m_drawingWavetable && m_wavetable.valid()) {
        const int64_t index = static_cast<int64_t>(
            static_cast<double>(event.x - m_wavetableArea.x) /
            std::max(1, m_wavetableArea.w) *
            (m_wavetable.frameSize() - 1));
        const float value = std::clamp(
            1.0f - 2.0f * static_cast<float>(
                              event.y - m_wavetableArea.y) /
                              std::max(1, m_wavetableArea.h),
            -1.0f, 1.0f);
        auto& frame = m_wavetable.frame(m_wavetableView.selectedFrame);
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
