#include "App.h"

#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"
#include "vwave/Processing.h"
#include "vwave/Synth.h"
#include "vwave/Version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

SDL_Color kBackground{14, 15, 19, 255};
SDL_Color kPanel{23, 25, 32, 255};
SDL_Color kPanelRaised{31, 34, 43, 255};
SDL_Color kContent{11, 12, 16, 255};
SDL_Color kBorder{52, 57, 70, 255};
SDL_Color kText{218, 222, 234, 255};
SDL_Color kTextDim{137, 145, 163, 255};
SDL_Color kTextFaint{88, 95, 112, 255};
SDL_Color kAccent{76, 201, 176, 255};
SDL_Color kAccentDark{31, 101, 91, 255};
SDL_Color kAccentText{194, 255, 238, 255};
SDL_Color kOrange{238, 168, 76, 255};
SDL_Color kRed{224, 92, 92, 255};
SDL_Color kBlue{86, 145, 230, 255};
SDL_Color kCanvas{16, 18, 24, 255};
SDL_Color kLane{15, 17, 23, 255};
SDL_Color kLaneActive{20, 25, 30, 255};
SDL_Color kMenu{16, 18, 24, 255};
SDL_Color kDisabled{27, 29, 36, 255};

std::string defaultSettingsPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base;
    if (xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / ".config";
    else
        base = std::filesystem::current_path();
    return (base / "vwave" / "settings.json").string();
}

void applyPalette(const std::string& theme) {
    if (theme == "light") {
        kBackground = {238, 239, 244, 255};
        kPanel = {228, 230, 237, 255};
        kPanelRaised = {216, 219, 229, 255};
        kContent = {249, 249, 252, 255};
        kBorder = {190, 194, 206, 255};
        kText = {32, 35, 44, 255};
        kTextDim = {88, 94, 108, 255};
        kTextFaint = {130, 136, 150, 255};
        kAccent = {13, 130, 116, 255};
        kAccentDark = {205, 240, 234, 255};
        kAccentText = {18, 82, 74, 255};
        kOrange = {170, 100, 15, 255};
        kRed = {190, 45, 55, 255};
        kBlue = {50, 95, 190, 255};
        kCanvas = {252, 252, 255, 255};
        kLane = {248, 249, 253, 255};
        kLaneActive = {235, 247, 245, 255};
        kMenu = {236, 238, 244, 255};
        kDisabled = {225, 227, 234, 255};
    } else {
        kBackground = {14, 15, 19, 255};
        kPanel = {23, 25, 32, 255};
        kPanelRaised = {31, 34, 43, 255};
        kContent = {11, 12, 16, 255};
        kBorder = {52, 57, 70, 255};
        kText = {218, 222, 234, 255};
        kTextDim = {137, 145, 163, 255};
        kTextFaint = {88, 95, 112, 255};
        kAccent = {76, 201, 176, 255};
        kAccentDark = {31, 101, 91, 255};
        kAccentText = {194, 255, 238, 255};
        kOrange = {238, 168, 76, 255};
        kRed = {224, 92, 92, 255};
        kBlue = {86, 145, 230, 255};
        kCanvas = {16, 18, 24, 255};
        kLane = {15, 17, 23, 255};
        kLaneActive = {20, 25, 30, 255};
        kMenu = {16, 18, 24, 255};
        kDisabled = {27, 29, 36, 255};
    }
}

std::string baseName(const std::string& path) {
    if (path.empty()) return "Untitled";
    return std::filesystem::path(path).filename().string();
}

std::string withExtension(std::string path, const std::string& extension) {
    if (path.empty()) return path;
    if (std::filesystem::path(path).extension().empty()) path += extension;
    return path;
}

std::string suggestedMixdownPath(const std::string& projectPath,
                                 const std::string& audioPath) {
    std::filesystem::path base =
        !projectPath.empty() ? std::filesystem::path(projectPath)
                             : std::filesystem::path(audioPath);
    if (base.empty())
        return (std::filesystem::current_path() / "mixdown.wav").string();
    return (base.parent_path() / (base.stem().string() + "_mix.wav")).string();
}

std::string formatSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "0.000 s";
    std::ostringstream out;
    if (seconds >= 60.0) {
        const int minutes = static_cast<int>(seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        out << minutes << ':'
            << std::setw(6) << std::setfill('0') << std::fixed
            << std::setprecision(3) << rest;
    } else {
        out << std::fixed << std::setprecision(3) << seconds << " s";
    }
    return out.str();
}

std::string formatRate(int sampleRate) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(sampleRate) / 1000.0 << " kHz";
    return out.str();
}

std::string truncateText(const std::string& text, TTF_Font* font, int maxWidth) {
    if (!font || maxWidth <= 0) return text;
    int width = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &width, nullptr) != 0) return text;
    if (width <= maxWidth) return text;
    std::string result = text;
    while (!result.empty()) {
        result.pop_back();
        if (TTF_SizeUTF8(font, (result + "...").c_str(), &width, nullptr) != 0)
            break;
        if (width <= maxWidth) return result + "...";
    }
    return "...";
}

Uint32 makePixel(float normalized, float alpha) {
    const float t = std::clamp(normalized, 0.0f, 1.0f);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (t < 0.25f) {
        const float u = t / 0.25f;
        r = 7.0f + (18.0f - 7.0f) * u;
        g = 9.0f + (46.0f - 9.0f) * u;
        b = 18.0f + (92.0f - 18.0f) * u;
    } else if (t < 0.5f) {
        const float u = (t - 0.25f) / 0.25f;
        r = 18.0f + (26.0f - 18.0f) * u;
        g = 46.0f + (166.0f - 46.0f) * u;
        b = 92.0f + (171.0f - 92.0f) * u;
    } else if (t < 0.75f) {
        const float u = (t - 0.5f) / 0.25f;
        r = 26.0f + (245.0f - 26.0f) * u;
        g = 166.0f + (162.0f - 166.0f) * u;
        b = 171.0f + (52.0f - 171.0f) * u;
    } else {
        const float u = (t - 0.75f) / 0.25f;
        r = 245.0f + (255.0f - 245.0f) * u;
        g = 162.0f + (248.0f - 162.0f) * u;
        b = 52.0f + (220.0f - 52.0f) * u;
    }
    const Uint8 a = static_cast<Uint8>(
        std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return (static_cast<Uint32>(a) << 24) |
           (static_cast<Uint32>(r) << 16) |
           (static_cast<Uint32>(g) << 8) |
           static_cast<Uint32>(b);
}

int64_t clampFrame(int64_t frame, int64_t frames) {
    return std::clamp<int64_t>(frame, 0, frames);
}

std::string formatRatio(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value << "x";
    return out.str();
}

std::string formatDb(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (value > 0.0) out << '+';
    out << value << " dB";
    return out.str();
}

std::string formatHz(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(value >= 1000.0 ? 2 : 0);
    if (value >= 1000.0) out << value / 1000.0 << " kHz";
    else out << value << " Hz";
    return out.str();
}

std::string formatInt(double value) {
    return std::to_string(static_cast<int>(std::lround(value)));
}

std::string formatMilliseconds(double value) {
    return std::to_string(static_cast<int>(std::lround(value))) + " ms";
}

std::string formatSecondsControl(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value << " s";
    return out.str();
}

std::string formatSemitones(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (value > 0.0) out << '+';
    out << value << " st";
    return out.str();
}

std::string formatMorph(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string formatMidiNote(double value) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F",
                                  "F#", "G", "G#", "A", "A#", "B"};
    const int note = std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    std::ostringstream out;
    out << names[note % 12] << (note / 12 - 1) << " (" << note << ')';
    return out.str();
}

const char* const kAlgorithmNames[] = {"WSOLA", "Phase Vocoder",
                                       "PaulStretch"};
const int kFrameSizes[] = {256, 512, 1024, 2048, 4096, 8192};
const char* const kSliceModeNames[] = {"Transients", "Beats", "Markers"};

} // namespace

App::App() {
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
}

App::~App() {
    m_recorder.stop();
    m_player.close();
    if (m_spectrogramTexture) SDL_DestroyTexture(m_spectrogramTexture);
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

    const char* regularCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    const char* boldCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    const char* regular = nullptr;
    const char* bold = nullptr;
    for (const char* candidate : regularCandidates) {
        if (std::filesystem::exists(candidate)) {
            regular = candidate;
            break;
        }
    }
    for (const char* candidate : boldCandidates) {
        if (std::filesystem::exists(candidate)) {
            bold = candidate;
            break;
        }
    }
    if (!regular) regular = "DejaVuSans.ttf";
    if (!bold) bold = regular;
    m_fontSmall = TTF_OpenFont(regular, 12);
    m_font = TTF_OpenFont(regular, 14);
    m_fontBold = TTF_OpenFont(bold, 15);
    m_fontTitle = TTF_OpenFont(bold, 23);
    if (!m_fontSmall) m_fontSmall = m_font;
    if (!m_fontBold) m_fontBold = m_font;
    if (!m_fontTitle) m_fontTitle = m_fontBold;

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
    setStatus("Drop an audio file or press Ctrl+O", 6000);
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
    m_buttons.clear();
    m_sliders.clear();
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

    if (m_timeline.empty()) {
        drawText("Open audio or create your first track", content.x + 40,
                 content.y + 60, kTextDim, fontTitle());
        drawText("Ctrl+O or drag and drop WAV, AIFF, FLAC, OGG",
                 content.x + 40, content.y + 96, kTextFaint, fontNormal());
        drawText("Add more tracks with Add Track. Every edit is undoable.",
                 content.x + 40, content.y + 118, kTextFaint, fontSmall());
    } else if (m_tab == vwave::EditorTab::Waveform) {
        drawWaveform(content);
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        drawSpectrum(content);
    } else {
        drawWavetable(content);
    }

    drawFooter();
    drawMenuDropdown();
    if (m_settingsOpen) drawSettingsDialog();
    if (m_dialogMode != DialogMode::None) drawDialog();
    SDL_RenderPresent(m_renderer);
}

void App::drawButton(const Button& button) {
    SDL_Color fill = button.accent ? kAccentDark : kPanelRaised;
    if (button.active) fill = kAccentDark;
    if (!button.enabled) fill = kDisabled;
    SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(m_renderer, &button.rect);
    SDL_SetRenderDrawColor(m_renderer,
                           button.enabled ? kBorder.r : kTextFaint.r,
                           button.enabled ? kBorder.g : kTextFaint.g,
                           button.enabled ? kBorder.b : kTextFaint.b, 255);
    SDL_RenderDrawRect(m_renderer, &button.rect);
    const SDL_Color textColor = !button.enabled
                                    ? kTextFaint
                                    : (button.accent ? kAccentText : kText);
    drawTextCentered(button.label, button.rect, textColor, fontSmall());
}

void App::addSlider(const std::string& label, const std::string& valueText,
                    int x, int y, int w, vwave::ParameterSlider& slider,
                    std::function<void(double)> onChanged) {
    drawText(label, x, y, kTextDim, fontSmall());
    const int valueWidth = textWidth(valueText, fontSmall());
    drawText(valueText, x + w - valueWidth, y, kText, fontSmall());
    SDL_Rect track{x, y + 17, w, 6};
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &track);
    const double normalized = slider.normalized();
    SDL_Rect filled{track.x, track.y, static_cast<int>(normalized * track.w),
                    track.h};
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 210);
    SDL_RenderFillRect(m_renderer, &filled);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &track);
    const int handleX = track.x + static_cast<int>(normalized * track.w);
    SDL_Rect handle{handleX - 3, track.y - 4, 7, track.h + 8};
    SDL_SetRenderDrawColor(m_renderer, kText.r, kText.g, kText.b, 255);
    SDL_RenderFillRect(m_renderer, &handle);
    m_sliders.push_back(
        {&slider, track, label, m_controlArea, std::move(onChanged)});
}

void App::setSliderFromX(SliderRef& slider, int x) {
    if (!slider.slider || slider.track.w <= 0) return;
    const double position =
        static_cast<double>(x - slider.track.x) / slider.track.w;
    slider.slider->setNormalized(position);
    if (slider.onChanged) slider.onChanged(slider.slider->value);
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
    clearAnalysis();
    m_wavetable = {};
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
    syncTrackControls();
    clearAnalysis();
    m_wavetable = {};
    m_wavetableView.selectedFrame = 0;
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = clampFrame(m_playhead, m_timeline.lengthFrames());
    m_audioPath = hasTracks() ? activeDocument().sourcePath() : std::string{};
    if (m_timeline.empty()) m_projectPath.clear();
    updateWindowTitle();
    setStatus("Removed track " + name);
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
    Button button;
    button.rect = {x, y, w, h};
    button.label = label;
    button.action = std::move(action);
    button.accent = accent;
    button.active = active;
    button.enabled = enabled;
    button.area = m_controlArea;
    m_buttons.push_back(button);
    drawButton(button);
}

int App::textWidth(const std::string& text, TTF_Font* font) const {
    if (!font || text.empty()) return 0;
    int width = 0;
    TTF_SizeUTF8(font, text.c_str(), &width, nullptr);
    return width;
}

int App::drawText(const std::string& text, int x, int y, const Color& color,
                  TTF_Font* font) {
    if (!font || text.empty()) return 0;
    SDL_Surface* surface =
        TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return 0;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    const int width = surface->w;
    const int height = surface->h;
    SDL_FreeSurface(surface);
    if (!texture) return 0;
    SDL_Rect target{x, y, width, height};
    SDL_RenderCopy(m_renderer, texture, nullptr, &target);
    SDL_DestroyTexture(texture);
    return width;
}

void App::drawTextCentered(const std::string& text, const SDL_Rect& rect,
                           const Color& color, TTF_Font* font) {
    if (!font) return;
    int width = 0;
    int height = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &width, &height) != 0) return;
    drawText(text, rect.x + (rect.w - width) / 2,
             rect.y + (rect.h - height) / 2, color, font);
}

void App::drawSection(const std::string& title, int& y) {
    drawText(title, 18, y, kAccent, fontSmall());
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 160);
    SDL_RenderDrawLine(m_renderer, 18, y + 17, m_sidebarRect.w - 18, y + 17);
    y += 25;
}

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
                   openDialog(DialogMode::Open, "Open audio or project", "",
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
                   if (!activeDocument().undo()) setStatus("Nothing to undo");
                   else {
                       markEdited();
                       setStatus("Undone");
                   }
               }, audio, "Ctrl+Z"),
        action("Redo",
               [this] {
                   m_player.stop();
                   if (!activeDocument().redo()) setStatus("Nothing to redo");
                   else {
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
        action(m_recording ? "Stop Recording" : "Record",
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
               [this] { pitchSelection(m_processTarget == 1); }, audio)};
    Menu view;
    view.title = "View";
    view.items = {
        action("Waveform", [this] { setTab(vwave::EditorTab::Waveform); },
               true, "1"),
        action("Spectrum", [this] { setTab(vwave::EditorTab::Spectrum); },
               true, "2"),
        action("Wavetable", [this] { setTab(vwave::EditorTab::Wavetable); },
               true, "3"),
        separator(),
        action("Zoom In", [this] { zoomTime(1.25); }, tracks, "+"),
        action("Zoom Out", [this] { zoomTime(0.8); }, tracks, "-"),
        action("Fit Time", [this] { fitTime(); }, tracks),
        action("Fit Frequency", [this] { fitFrequency(); }, tracks)};
    Menu settings;
    settings.title = "Settings";
    settings.items = {
        action("Settings...", [this] { openSettings(); }, true, "Ctrl+,"),
        separator(),
        action("Dark Theme", [this] { setTheme("dark"); },
               m_settings.theme != "dark"),
        action("Light Theme", [this] { setTheme("light"); },
               m_settings.theme != "light")};
    m_menus = {file, edit, track, process, view, settings};

    SDL_SetRenderDrawColor(m_renderer, kMenu.r, kMenu.g, kMenu.b, 255);
    SDL_RenderFillRect(m_renderer, &m_menubarRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, m_menubarRect.y + m_menubarRect.h - 1,
                       m_menubarRect.w, m_menubarRect.y + m_menubarRect.h - 1);
    drawText("VWAVE", 14, 4, kAccent, fontBold());
    int x = 78;
    for (int index = 0; index < static_cast<int>(m_menus.size()); ++index) {
        Menu& menu = m_menus[static_cast<size_t>(index)];
        const int width = textWidth(menu.title, fontNormal()) + 22;
        menu.titleRect = {x, 0, width, m_menubarRect.h};
        if (index == m_openMenu) {
            SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                                   kPanelRaised.b, 255);
            SDL_RenderFillRect(m_renderer, &menu.titleRect);
        }
        drawTextCentered(menu.title, menu.titleRect, kText, fontNormal());
        x += width + 2;
    }
    if (m_openMenu >= static_cast<int>(m_menus.size())) m_openMenu = -1;
}

void App::drawMenuDropdown() {
    m_menuItemRects.clear();
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
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_openMenuRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_openMenuRect);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    SDL_Point mouse{mouseX, mouseY};
    int y = m_openMenuRect.y + 8;
    m_menuItemRects.resize(menu.items.size());
    for (size_t index = 0; index < menu.items.size(); ++index) {
        const MenuItem& item = menu.items[index];
        if (item.separator) {
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 100);
            SDL_RenderDrawLine(m_renderer, m_openMenuRect.x + 8, y + 3,
                               m_openMenuRect.x + m_openMenuRect.w - 8, y + 3);
            m_menuItemRects[index] = {};
            y += 7;
            continue;
        }
        SDL_Rect row{m_openMenuRect.x + 1, y, m_openMenuRect.w - 2, 24};
        m_menuItemRects[index] = row;
        if (item.enabled && SDL_PointInRect(&mouse, &row)) {
            SDL_SetRenderDrawColor(m_renderer, kAccentDark.r, kAccentDark.g,
                                   kAccentDark.b, 255);
            SDL_RenderFillRect(m_renderer, &row);
        }
        const SDL_Color color = item.enabled ? kText : kTextFaint;
        drawText(item.label, row.x + 12, row.y + 4, color, fontNormal());
        if (!item.shortcut.empty()) {
            const int shortcutWidth = textWidth(item.shortcut, fontSmall());
            drawText(item.shortcut, row.x + row.w - shortcutWidth - 10,
                     row.y + 5, kTextFaint, fontSmall());
        }
        y += 24;
    }
}

void App::drawToolbar() {
    m_controlArea = 0;
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_toolbarRect);
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 255);
    SDL_Rect accentLine{0, m_toolbarRect.y + m_toolbarRect.h - 2,
                        m_toolbarRect.w, 2};
    SDL_RenderFillRect(m_renderer, &accentLine);

    const int tabY = m_toolbarRect.y + 13;
    addButton("Waveform", 154, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Waveform); }, false,
              m_tab == vwave::EditorTab::Waveform);
    addButton("Spectrum", 254, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Spectrum); }, false,
              m_tab == vwave::EditorTab::Spectrum);
    addButton("Wavetable", 354, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Wavetable); }, false,
              m_tab == vwave::EditorTab::Wavetable);

    int x = 468;
    addButton("Open", x, tabY, 60, 29,
              [this] { openDialog(DialogMode::Open, "Open audio or project",
                                  "", false); });
    x += 66;
    addButton("Add Track", x, tabY, 76, 29,
              [this] { openDialog(DialogMode::AddTrack, "Add audio track",
                                  "", false); });
    x += 82;
    addButton("Save WAV", x, tabY, 78, 29,
              [this] {
                  openDialog(DialogMode::SaveAudio, "Export mixdown",
                             suggestedMixdownPath(m_projectPath, m_audioPath),
                             true);
              });
    x += 84;
    addButton("Project", x, tabY, 66, 29,
              [this] { openDialog(DialogMode::SaveProject, "Save project",
                                  m_projectPath, true); });
    x += 72;
    addButton(m_player.isPlaying() ? "Playing" : "Play", x, tabY, 64, 29,
              [this] { togglePlay(); }, true,
              m_player.isPlaying(), !m_timeline.empty());
    x += 70;
    addButton("Stop", x, tabY, 54, 29, [this] { stopPlayback(); },
              false, false, m_player.isPlaying());
    x += 64;
    addButton("-", x, tabY, 26, 29, [this] { zoomTime(0.8); }, false, false,
              !m_timeline.empty());
    x += 30;
    addButton("+", x, tabY, 26, 29, [this] { zoomTime(1.25); }, false, false,
              !m_timeline.empty());
    x += 30;
    addButton("Fit", x, tabY, 40, 29, [this] { fitTime(); }, false, false,
              !m_timeline.empty());
}

void App::drawSidebar() {
    m_controlArea = 1;
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_sidebarRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawLine(m_renderer, m_sidebarRect.w - 1, m_sidebarRect.y,
                       m_sidebarRect.w - 1,
                       m_sidebarRect.y + m_sidebarRect.h);

    SDL_RenderSetClipRect(m_renderer, &m_sidebarRect);
    const int contentTop = m_sidebarRect.y + 14;
    const int previousMaximum =
        std::max(0, m_sidebarContentHeight - m_sidebarRect.h);
    m_sidebarScroll =
        std::clamp(m_sidebarScroll, 0.0, static_cast<double>(previousMaximum));
    int y = contentTop - static_cast<int>(std::lround(m_sidebarScroll));

    drawSection("PROJECT", y);
    const std::string name = m_projectPath.empty() ? m_audioPath : m_projectPath;
    drawText(truncateText(baseName(name), fontBold(), 250), 18, y, kText,
             fontBold());
    y += 22;
    {
        std::ostringstream audioInfo;
        audioInfo << formatRate(m_timeline.sampleRate()) << "  "
                  << m_timeline.trackCount() << " track"
                  << (m_timeline.trackCount() == 1 ? "" : "s") << "  "
                  << formatSeconds(m_timeline.duration());
        drawText(audioInfo.str(), 18, y, kTextDim, fontSmall());
    }
    y += 18;
    if (hasTracks()) {
        drawText(truncateText("Active: " + m_timeline.track(m_timeline.activeTrack()).name,
                              fontSmall(), 250),
                 18, y, kAccent, fontSmall());
    }
    y += 18;
    {
        std::string history = "No edits";
        if (activeDocument().canUndo())
            history = "Undo: " + activeDocument().undoLabel();
        drawText(truncateText(history, fontSmall(), 250), 18, y, kTextFaint,
                 fontSmall());
    }
    y += 18;
    {
        std::string selection = "Selection: none";
        if (m_timeline.selection().active) {
            const vwave::Selection sel = m_timeline.selection();
            std::ostringstream out;
            out << "Selection: "
                << formatSeconds(static_cast<double>(sel.start) /
                                 std::max(1, m_timeline.sampleRate()))
                << " - "
                << formatSeconds(static_cast<double>(sel.end) /
                                 std::max(1, m_timeline.sampleRate()));
            selection = out.str();
        }
        drawText(truncateText(selection, fontSmall(), 250), 18, y, kTextDim,
                 fontSmall());
    }
    y += 18;
    if (m_hasBeats || m_hasPitch) {
        std::ostringstream analysis;
        if (m_hasBeats) {
            analysis << std::fixed << std::setprecision(1) << m_beats.bpm
                     << " BPM";
        }
        if (m_hasPitch) {
            if (m_hasBeats) analysis << "  ";
            analysis << std::fixed << std::setprecision(1) << m_pitch.frequency
                     << " Hz";
        }
        drawText(analysis.str(), 18, y, kOrange, fontSmall());
        y += 18;
    }
    y += 8;

    const bool hasAudio = !activeDocument().audio().empty();
    const int buttonWidth = 128;
    const int buttonHeight = 22;
    const int gap = 8;
    auto grid = [&](std::initializer_list<std::pair<std::string,
                                                     std::function<void()>>>
                        items) {
        int column = 0;
        for (const auto& item : items) {
            const int bx = 18 + column * (buttonWidth + gap);
            addButton(item.first, bx, y, buttonWidth, buttonHeight, item.second,
                      false, false, hasAudio);
            ++column;
            if (column == 2) {
                column = 0;
                y += buttonHeight + 6;
            }
        }
        if (column != 0) y += buttonHeight + 6;
    };
    auto fullButton = [&](const std::string& label,
                          std::function<void()> action) {
        addButton(label, 18, y, buttonWidth * 2 + gap, buttonHeight,
                  std::move(action), false, false, hasAudio);
        y += buttonHeight + 6;
    };

    if (m_tab == vwave::EditorTab::Spectrum) {
        drawSection("SPECTRAL", y);
        std::string regionText = "Region: whole document";
        if (m_spectralRegion.active && !m_spectralRegion.frames.empty()) {
            std::ostringstream out;
            out << "Region: "
                << formatSeconds(static_cast<double>(
                                     m_spectralRegion.frames.start) /
                                 std::max(1, activeDocument().sampleRate()))
                << " - "
                << formatSeconds(static_cast<double>(
                                     m_spectralRegion.frames.end) /
                                 std::max(1, activeDocument().sampleRate()))
                << "  " << formatHz(m_spectralRegion.lowHz) << " - "
                << formatHz(m_spectralRegion.highHz);
            regionText = out.str();
        } else if (activeDocument().selection().active) {
            regionText = "Region: current track selection";
        }
        drawText(truncateText(regionText, fontSmall(), 256), 18, y, kTextFaint,
                 fontSmall());
        y += 18;
        grid({{"Freq -", [this] { zoomFrequency(0.5); }},
              {"Freq +", [this] { zoomFrequency(2.0); }}});
        fullButton("Fit Frequency", [this] { fitFrequency(); });
        addSlider("Low frequency", formatHz(m_spectralLow.value), 18, y, 256,
                  m_spectralLow);
        y += 30;
        addSlider("High frequency", formatHz(m_spectralHigh.value), 18, y, 256,
                  m_spectralHigh);
        y += 30;
        addSlider("Band gain", formatDb(m_spectralGain.value), 18, y, 256,
                  m_spectralGain);
        y += 30;
        grid({{"Apply Gain",
               [this] { applySpectralGain(m_spectralGain.value); }},
              {"Erase", [this] { applySpectralGain(-120.0); }}});
        addSlider("Gate threshold", formatDb(m_spectralGate.value), 18, y, 256,
                  m_spectralGate);
        y += 30;
        grid({{"Apply Gate", [this] { applySpectralGate(); }},
              {"Filter: " + std::string(m_filterLowpass ? "LPF" : "HPF"),
               [this] { m_filterLowpass = !m_filterLowpass; }}});
        addSlider("Filter cutoff", formatHz(m_spectralCutoff.value), 18, y, 256,
                  m_spectralCutoff);
        y += 30;
        grid({{"Apply Filter",
               [this] { applySpectralFilter(m_filterLowpass); }},
              {"Apply Blur", [this] { applySpectralBlur(); }}});
        addSlider("Blur time", formatInt(m_spectralBlurTime.value), 18, y, 256,
                  m_spectralBlurTime);
        y += 30;
        addSlider("Blur frequency", formatInt(m_spectralBlurFrequency.value),
                  18, y, 256, m_spectralBlurFrequency);
        y += 30;
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        drawSection("WAVETABLE", y);
        grid({{"Size: " + std::to_string(kFrameSizes[m_frameSizeIndex]),
               [this] {
                   m_frameSizeIndex =
                       (m_frameSizeIndex + 1) %
                       static_cast<int>(sizeof(kFrameSizes) /
                                        sizeof(kFrameSizes[0]));
               }},
              {"Extract", [this] { extractWavetable(); }}});
        addSlider("Frame count", formatInt(m_wavetableFrameCount.value), 18, y,
                  256, m_wavetableFrameCount);
        y += 30;
        grid({{"Save", [this] {
                   openDialog(DialogMode::SaveWavetable, "Save wavetable",
                              (std::filesystem::current_path() /
                               "wavetable.vwt").string(),
                              true);
               }},
              {"Normalize", [this] {
                   if (!m_wavetable.valid()) return;
                   m_wavetable.normalizeFrames();
                   setStatus("Wavetable normalized");
               }}});
        drawSection("SYNTH", y);
        addSlider("Note", formatMidiNote(m_synthNote.value), 18, y, 256,
                  m_synthNote);
        y += 30;
        addSlider("Root", formatMidiNote(m_synthRoot.value), 18, y, 256,
                  m_synthRoot);
        y += 30;
        addSlider("Duration", formatSecondsControl(m_synthDuration.value), 18,
                  y, 256, m_synthDuration);
        y += 30;
        addSlider("Morph", formatMorph(m_synthMorph.value), 18, y, 256,
                  m_synthMorph);
        y += 30;
        addSlider("Unison", formatInt(m_synthUnison.value), 18, y, 256,
                  m_synthUnison);
        y += 30;
        addSlider("Level", formatMorph(m_synthGain.value), 18, y, 256,
                  m_synthGain);
        y += 30;
        fullButton("Render Note", [this] { renderWavetableNote(); });
        grid({{"Prev Frame", [this] {
                   if (!m_wavetable.valid()) return;
                   --m_wavetableView.selectedFrame;
                   m_wavetableView.clamp(m_wavetable.frameCount());
               }},
              {"Next Frame", [this] {
                   if (!m_wavetable.valid()) return;
                   ++m_wavetableView.selectedFrame;
                   m_wavetableView.clamp(m_wavetable.frameCount());
               }}});
    }

    drawSection("EDIT", y);
    grid({{"Undo", [this] {
               m_player.stop();
               if (!activeDocument().undo()) {
                   setStatus("Nothing to undo");
               } else {
                   markEdited();
                   setStatus("Undone");
               }
           }},
          {"Redo", [this] {
               m_player.stop();
               if (!activeDocument().redo()) {
                   setStatus("Nothing to redo");
               } else {
                   markEdited();
                   setStatus("Redone");
               }
           }},
          {"Fade In", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().fadeIn(error);
               }, "Fade in applied");
           }},
          {"Fade Out", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().fadeOut(error);
               }, "Fade out applied");
           }},
          {"Reverse", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().reverseSelection(error);
               }, "Reversed");
           }},
          {"Invert", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().invertSelection(error);
               }, "Polarity inverted");
           }},
          {"Delete", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().deleteSelection(error);
               }, "Selection deleted");
           }},
          {"Crop", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().cropToSelection(error);
               }, "Cropped to selection");
           }},
          {"Select All", [this] {
               m_timeline.setSelection(0, m_timeline.lengthFrames());
               setStatus("Selected everything");
           }},
          {"Clear Sel", [this] {
               m_timeline.clearSelection();
               setStatus("Selection cleared");
           }},
          {"Copy", [this] {
               setStatus(activeDocument().copySelection() ? "Copied selection"
                                                    : "Select audio first");
           }},
          {"Cut", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().cutSelection(error);
               }, "Cut selection");
           }},
          {"Paste", [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().paste(error);
               }, "Pasted");
           }},
          {"Sel to Track", [this] { copySelectionToNewTrack(); }}});

    drawSection("GAIN", y);
    addSlider("Gain", formatDb(m_gainDb.value), 18, y, 256, m_gainDb);
    y += 30;
    addSlider("Normalize peak", formatDb(m_normalizePeak.value), 18, y, 256,
              m_normalizePeak);
    y += 30;
    grid({{"Apply Gain",
           [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().gainDb(m_gainDb.value, error);
               }, "Gain applied");
           }},
          {"Apply Normalize",
           [this] {
               runEdit([this](std::string* error) {
                   return activeDocument().normalize(m_normalizePeak.value, error);
               }, "Normalized");
           }}});

    drawSection("TIME & PITCH", y);
    addSlider("Stretch", formatRatio(m_stretchRatio.value), 18, y, 256,
              m_stretchRatio);
    y += 30;
    addSlider("Pitch", formatSemitones(m_pitchSemitones.value), 18, y, 256,
              m_pitchSemitones);
    y += 30;
    grid({{"Algorithm: " +
               std::string(m_stretchAlgorithm == 0
                               ? "WSOLA"
                               : (m_stretchAlgorithm == 1 ? "Phase" : "Paul")),
           [this] { m_stretchAlgorithm = (m_stretchAlgorithm + 1) % 3; }},
          {"Target: " + std::string(m_processTarget == 0 ? "Track"
                                                         : "New Track"),
           [this] { m_processTarget = (m_processTarget + 1) % 2; }}});
    grid({{"Apply Stretch",
           [this] { stretchSelection(m_processTarget == 1); }},
          {"Apply Pitch",
           [this] { pitchSelection(m_processTarget == 1); }}});

    drawSection("ANALYZE", y);
    addSlider("Beat sensitivity", formatMorph(m_beatSensitivity.value), 18, y,
              256, m_beatSensitivity);
    y += 30;
    grid({{"Beats", [this] { analyzeBeats(); }},
          {"Pitch", [this] { detectPitch(); }}});
    addSlider("Slice sensitivity", formatMorph(m_sliceSensitivity.value), 18,
              y, 256, m_sliceSensitivity);
    y += 30;
    addSlider("Minimum slice", formatMilliseconds(m_minSliceMs.value), 18, y,
              256, m_minSliceMs);
    y += 30;
    addSlider("Beats / slice", formatInt(m_beatsPerSlice.value), 18, y, 256,
              m_beatsPerSlice);
    y += 30;
    grid({{"Mode: " + std::string(kSliceModeNames[m_sliceMode]),
           [this] { m_sliceMode = (m_sliceMode + 1) % 3; }},
          {"Slice", [this] { sliceCurrent(); }}});
    fullButton("Export Slices", [this] {
        openDialog(DialogMode::ExportSlices, "Export slices to directory",
                   (std::filesystem::current_path() / "slices").string(), true);
    });

    m_sidebarContentHeight = y + static_cast<int>(std::lround(m_sidebarScroll)) -
                             contentTop + 16;
    const int maximumScroll =
        std::max(0, m_sidebarContentHeight - m_sidebarRect.h);
    m_sidebarScroll =
        std::clamp(m_sidebarScroll, 0.0, static_cast<double>(maximumScroll));
    SDL_RenderSetClipRect(m_renderer, nullptr);

    if (maximumScroll > 0) {
        SDL_Rect track{m_sidebarRect.w - 8, m_sidebarRect.y + 4, 5,
                       m_sidebarRect.h - 8};
        SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                               kPanelRaised.b, 230);
        SDL_RenderFillRect(m_renderer, &track);
        const int handleHeight = std::max(
            24, static_cast<int>(static_cast<double>(track.h) *
                                 m_sidebarRect.h / m_sidebarContentHeight));
        const int handleY = track.y + static_cast<int>(
                                          (static_cast<double>(track.h) -
                                           handleHeight) *
                                          (m_sidebarScroll / maximumScroll));
        SDL_Rect handle{track.x, handleY, track.w, handleHeight};
        SDL_SetRenderDrawColor(m_renderer, kTextFaint.r, kTextFaint.g,
                               kTextFaint.b, 255);
        SDL_RenderFillRect(m_renderer, &handle);
    }
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
                                   "1/2/3 views | Ctrl+Z undo";
    drawText(truncateText(left, fontSmall(), m_footerRect.w - 380),
             m_footerRect.x + 14, m_footerRect.y + 7,
             statusActive ? kAccent : kTextFaint, fontSmall());
    if (m_recording) {
        std::ostringstream recording;
        const int64_t frames = m_recordedInterleaved.size() /
                               std::max(1, m_recordedChannels);
        recording << "REC " << formatSeconds(
            static_cast<double>(frames) /
            std::max(1, m_recordedSampleRate));
        const std::string text = recording.str();
        drawText(text, m_footerRect.w - textWidth(text, fontSmall()) - 14,
                 m_footerRect.y + 7, kRed, fontSmall());
    } else {
        const std::string right =
            "F9 record | B beats | T slice | Wheel zoom | 1/2/3 views";
        drawText(right, m_footerRect.w - textWidth(right, fontSmall()) - 14,
                 m_footerRect.y + 7, kTextFaint, fontSmall());
    }
}

void App::drawWaveform(const SDL_Rect& area) {
    m_trackHeaderRects.clear();
    m_trackLaneRects.clear();
    m_trackHeaderRects.resize(static_cast<size_t>(m_timeline.trackCount()));
    m_trackLaneRects.resize(static_cast<size_t>(m_timeline.trackCount()));

    const int margin = 10;
    const int headerWidth = 232;
    const int rulerHeight = 24;
    const int overviewHeight = 30;
    const int rowsTop = area.y + rulerHeight + overviewHeight + 8;
    const int laneX = area.x + margin + headerWidth + 8;
    const int laneWidth = area.w - margin * 2 - headerWidth - 8;
    const int laneHeight = area.h - rulerHeight - overviewHeight - 18;
    m_trackHeaderColumn = {area.x + margin, rowsTop, headerWidth - 8, laneHeight};
    m_tracksArea = {laneX, rowsTop, laneWidth, laneHeight};
    m_waveArea = m_tracksArea;
    m_overviewArea = {laneX, area.y + rulerHeight + 2, laneWidth,
                      overviewHeight - 8};

    drawText("TRACKS", area.x + margin + 2, area.y + 8, kAccent, fontSmall());
    drawText(std::to_string(m_timeline.trackCount()) + " track" +
                 (m_timeline.trackCount() == 1 ? "" : "s"),
             area.x + margin + 2, area.y + 24, kTextFaint, fontSmall());

    const double totalFrames = std::max(
        1.0, static_cast<double>(m_timeline.lengthFrames()));
    const int ticks = 10;
    for (int i = 0; i <= ticks; ++i) {
        const double frame = m_view.startFrame + m_view.visibleFrames * i / ticks;
        const int x = laneX + laneWidth * i / ticks;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
        SDL_RenderDrawLine(m_renderer, x, rowsTop, x, rowsTop + laneHeight);
        drawText(formatSeconds(frame / std::max(1, m_timeline.sampleRate())),
                 x + 3, area.y + 7, kTextFaint, fontSmall());
    }

    SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                           kPanelRaised.b, 255);
    SDL_RenderFillRect(m_renderer, &m_overviewArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_overviewArea);
    ensureOverview();
    if (!m_overviewPeaks.empty() && hasTracks()) {
        const auto& active = m_timeline.track(m_timeline.activeTrack());
        const double trackStart = static_cast<double>(active.startFrame) /
                                  totalFrames;
        const double trackEnd =
            static_cast<double>(active.startFrame +
                                active.document.audio().numFrames()) /
            totalFrames;
        const int x0 = m_overviewArea.x +
                       static_cast<int>(trackStart * m_overviewArea.w);
        const int x1 = m_overviewArea.x +
                       static_cast<int>(trackEnd * m_overviewArea.w);
        const int center = m_overviewArea.y + m_overviewArea.h / 2;
        SDL_SetRenderDrawColor(m_renderer, kTextDim.r, kTextDim.g, kTextDim.b,
                               110);
        SDL_RenderDrawLine(m_renderer, m_overviewArea.x, center,
                           m_overviewArea.x + m_overviewArea.w, center);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 160);
        const int width = std::max(1, x1 - x0);
        for (int i = 0; i < width; ++i) {
            const size_t index = static_cast<size_t>(
                static_cast<double>(i) / width *
                static_cast<double>(m_overviewPeaks.size()));
            if (index >= m_overviewPeaks.size()) continue;
            const float minimum = m_overviewPeaks[index].minimum;
            const float maximum = m_overviewPeaks[index].maximum;
            const int y0 = center - static_cast<int>(
                                          maximum *
                                          (m_overviewArea.h / 2 - 2));
            const int y1 = center - static_cast<int>(
                                          minimum *
                                          (m_overviewArea.h / 2 - 2));
            SDL_RenderDrawLine(m_renderer, x0 + i, y0, x0 + i, y1);
        }
        const double viewStart = m_view.startFrame / totalFrames;
        const double viewEnd =
            (m_view.startFrame + m_view.visibleFrames) / totalFrames;
        SDL_Rect viewBox{
            m_overviewArea.x + static_cast<int>(viewStart * m_overviewArea.w),
            m_overviewArea.y + 2,
            std::max(2, static_cast<int>((viewEnd - viewStart) *
                                         m_overviewArea.w)),
            m_overviewArea.h - 4};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b, 220);
        SDL_RenderDrawRect(m_renderer, &viewBox);
        if (m_timeline.selection().active) {
            const double selectionStart =
                static_cast<double>(m_timeline.selection().start) / totalFrames;
            const double selectionEnd =
                static_cast<double>(m_timeline.selection().end) / totalFrames;
            SDL_Rect selection{
                m_overviewArea.x +
                    static_cast<int>(selectionStart * m_overviewArea.w),
                m_overviewArea.y + 1,
                std::max(1, static_cast<int>((selectionEnd - selectionStart) *
                                             m_overviewArea.w)),
                m_overviewArea.h - 2};
            SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                   kAccent.b, 80);
            SDL_RenderFillRect(m_renderer, &selection);
        }
        if (m_playhead >= 0 && m_playhead <= m_timeline.lengthFrames()) {
            const int x = m_overviewArea.x + static_cast<int>(
                static_cast<double>(m_playhead) / totalFrames *
                m_overviewArea.w);
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                   kOrange.b, 255);
            SDL_RenderDrawLine(m_renderer, x, m_overviewArea.y, x,
                               m_overviewArea.y + m_overviewArea.h);
        }
    }

    const int rowHeight = std::max(
        112, laneHeight / std::max(1, m_timeline.trackCount()));
    m_trackContentHeight = m_timeline.trackCount() * rowHeight;
    const int maximumScroll = std::max(0, m_trackContentHeight - laneHeight);
    m_trackScroll = std::clamp(m_trackScroll, 0.0,
                               static_cast<double>(maximumScroll));

    m_controlArea = 2;
    SDL_Rect rowsClip{area.x + margin, rowsTop, headerWidth + 8 + laneWidth,
                      laneHeight};
    SDL_RenderSetClipRect(m_renderer, &rowsClip);
    for (int index = 0; index < m_timeline.trackCount(); ++index) {
        const int rowY = rowsTop + index * rowHeight -
                         static_cast<int>(std::lround(m_trackScroll));
        if (rowY + rowHeight <= rowsTop || rowY >= rowsTop + laneHeight)
            continue;
        SDL_Rect header{area.x + margin, rowY + 2, headerWidth - 8,
                        rowHeight - 6};
        SDL_Rect lane{laneX, rowY + 2, laneWidth, rowHeight - 6};
        m_trackHeaderRects[static_cast<size_t>(index)] = header;
        m_trackLaneRects[static_cast<size_t>(index)] = lane;
        drawTrackHeader(header, index);

        const auto& track = m_timeline.track(index);
        const bool active = index == m_timeline.activeTrack();
        SDL_SetRenderDrawColor(m_renderer,
                               active ? kLaneActive.r : kLane.r,
                               active ? kLaneActive.g : kLane.g,
                               active ? kLaneActive.b : kLane.b, 255);
        SDL_RenderFillRect(m_renderer, &lane);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b,
                               active ? 220 : 90);
        SDL_RenderDrawRect(m_renderer, &lane);

        const bool audible =
            !track.muted && (!m_timeline.anySolo() || track.solo);
        const vwave::Audio& audio = track.document.audio();
        if (audio.empty()) {
            drawText("Empty track", lane.x + 12, lane.y + lane.h / 2 - 8,
                     kTextFaint, fontSmall());
            continue;
        }
        const int64_t viewStart =
            static_cast<int64_t>(std::floor(m_view.startFrame));
        const int64_t viewEnd = static_cast<int64_t>(
            std::ceil(m_view.startFrame + m_view.visibleFrames));
        const int64_t localStart = std::max<int64_t>(
            0, viewStart - track.startFrame);
        const int64_t localEnd = std::min<int64_t>(
            audio.numFrames(), viewEnd - track.startFrame);
        if (localEnd <= localStart) continue;
        const double timelineStart =
            static_cast<double>(track.startFrame + localStart);
        const int x0 = lane.x + static_cast<int>(
                                   (timelineStart - m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   lane.w);
        const int x1 = lane.x + static_cast<int>(
                                   (static_cast<double>(track.startFrame +
                                                        localEnd) -
                                    m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   lane.w);
        const int width = std::max(1, x1 - x0);
        const int channelCount =
            std::min(2, std::max(1, audio.numChannels()));
        const int laneGap = 3;
        const int channelHeight =
            (lane.h - 10 - (channelCount - 1) * laneGap) / channelCount;
        for (int ch = 0; ch < channelCount; ++ch) {
            SDL_Rect channelLane{lane.x, lane.y + 5 +
                                                ch * (channelHeight + laneGap),
                                 lane.w, channelHeight};
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 70);
            SDL_RenderDrawLine(m_renderer, channelLane.x,
                               channelLane.y + channelHeight / 2,
                               channelLane.x + channelLane.w,
                               channelLane.y + channelHeight / 2);
            const std::vector<vwave::PeakBin> peaks =
                vwave::computePeaks(audio, ch, {localStart, localEnd}, width);
            const int center = channelLane.y + channelHeight / 2;
            SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                   kAccent.b,
                                   audible ? (active ? 230 : 150) : 65);
            for (int i = 0; i < width; ++i) {
                if (x0 + i < lane.x || x0 + i >= lane.x + lane.w) continue;
                const float minimum = peaks[static_cast<size_t>(i)].minimum;
                const float maximum = peaks[static_cast<size_t>(i)].maximum;
                int y0 = center - static_cast<int>(maximum *
                                                   (channelHeight / 2 - 3));
                int y1 = center - static_cast<int>(minimum *
                                                   (channelHeight / 2 - 3));
                if (y0 == y1) {
                    --y0;
                    ++y1;
                }
                SDL_RenderDrawLine(m_renderer, x0 + i, y0, x0 + i, y1);
            }
        }
    }
    SDL_RenderSetClipRect(m_renderer, &m_contentRect);
    m_controlArea = 1;

    SDL_RenderSetClipRect(m_renderer, &m_tracksArea);
    if (m_hasBeats && hasTracks()) {
        const auto& active = m_timeline.track(m_timeline.activeTrack());
        for (size_t i = 0; i < m_beats.beats.size(); ++i) {
            const int64_t beat =
                m_beats.beats[i] + active.startFrame;
            if (beat < m_view.startFrame ||
                beat > m_view.startFrame + m_view.visibleFrames)
                continue;
            const int x = laneX + static_cast<int>(
                (static_cast<double>(beat) - m_view.startFrame) /
                std::max(1.0, m_view.visibleFrames) * laneWidth);
            SDL_SetRenderDrawColor(m_renderer,
                                   (i % 4 == 0) ? kBlue.r : kBorder.r,
                                   (i % 4 == 0) ? kBlue.g : kBorder.g,
                                   (i % 4 == 0) ? kBlue.b : kBorder.b,
                                   (i % 4 == 0) ? 150 : 80);
            SDL_RenderDrawLine(m_renderer, x, rowsTop, x, rowsTop + laneHeight);
        }
    }

    if (hasTracks() &&
        m_timeline.activeTrack() <
            static_cast<int>(m_trackLaneRects.size())) {
        const auto& active = m_timeline.track(m_timeline.activeTrack());
        const SDL_Rect activeLane =
            m_trackLaneRects[static_cast<size_t>(m_timeline.activeTrack())];
        if (activeLane.w > 0) {
            for (const auto& slice : active.document.slices()) {
                const int64_t frame = slice.range.start + active.startFrame;
                if (frame < m_view.startFrame ||
                    frame > m_view.startFrame + m_view.visibleFrames)
                    continue;
                const int x = laneX + static_cast<int>(
                    (static_cast<double>(frame) - m_view.startFrame) /
                    std::max(1.0, m_view.visibleFrames) * laneWidth);
                SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                       kAccent.b, 200);
                SDL_RenderDrawLine(m_renderer, x, activeLane.y, x,
                                   activeLane.y + activeLane.h);
            }
            for (const auto& marker : active.document.markers()) {
                const int64_t frame = marker.frame + active.startFrame;
                if (frame < m_view.startFrame ||
                    frame > m_view.startFrame + m_view.visibleFrames)
                    continue;
                const int x = laneX + static_cast<int>(
                    (static_cast<double>(frame) - m_view.startFrame) /
                    std::max(1.0, m_view.visibleFrames) * laneWidth);
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 220);
                SDL_RenderDrawLine(m_renderer, x, activeLane.y, x,
                                   activeLane.y + activeLane.h);
                drawText(marker.name, x + 3, activeLane.y + 3, kOrange,
                         fontSmall());
            }
        }
    }

    if (m_timeline.selection().active) {
        const double start = static_cast<double>(m_timeline.selection().start);
        const double end = static_cast<double>(m_timeline.selection().end);
        const int x0 = laneX + static_cast<int>(
            (start - m_view.startFrame) / std::max(1.0, m_view.visibleFrames) *
            laneWidth);
        const int x1 = laneX + static_cast<int>(
            (end - m_view.startFrame) / std::max(1.0, m_view.visibleFrames) *
            laneWidth);
        const int left = std::clamp(std::min(x0, x1), laneX, laneX + laneWidth);
        const int right = std::clamp(std::max(x0, x1), laneX, laneX + laneWidth);
        SDL_Rect selection{left, rowsTop, std::max(1, right - left), laneHeight};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 50);
        SDL_RenderFillRect(m_renderer, &selection);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 220);
        SDL_RenderDrawLine(m_renderer, left, rowsTop, left,
                           rowsTop + laneHeight);
        SDL_RenderDrawLine(m_renderer, right, rowsTop, right,
                           rowsTop + laneHeight);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 150);
        SDL_RenderDrawLine(m_renderer, laneX, rowsTop + laneHeight, laneX + laneWidth,
                           rowsTop + laneHeight);
    }

    if (m_playhead >= m_view.startFrame &&
        m_playhead <= m_view.startFrame + m_view.visibleFrames) {
        const int x = laneX + static_cast<int>(
            (static_cast<double>(m_playhead) - m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * laneWidth);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               255);
        SDL_RenderDrawLine(m_renderer, x, rowsTop, x, rowsTop + laneHeight);
    }
    SDL_RenderSetClipRect(m_renderer, nullptr);

    if (maximumScroll > 0) {
        SDL_Rect track{m_trackHeaderColumn.x + m_trackHeaderColumn.w - 4,
                       m_trackHeaderColumn.y, 4, m_trackHeaderColumn.h};
        SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                               kPanelRaised.b, 200);
        SDL_RenderFillRect(m_renderer, &track);
        const int handleHeight = std::max(
            20, static_cast<int>(static_cast<double>(track.h) * laneHeight /
                                 m_trackContentHeight));
        const int handleY = track.y + static_cast<int>(
                                          (static_cast<double>(track.h) -
                                           handleHeight) *
                                          (m_trackScroll / maximumScroll));
        SDL_Rect handle{track.x, handleY, track.w, handleHeight};
        SDL_SetRenderDrawColor(m_renderer, kTextFaint.r, kTextFaint.g,
                               kTextFaint.b, 255);
        SDL_RenderFillRect(m_renderer, &handle);
    }
}

void App::drawTrackHeader(const SDL_Rect& header, int index) {
    const auto& track = m_timeline.track(index);
    const bool active = index == m_timeline.activeTrack();
    SDL_SetRenderDrawColor(m_renderer, active ? kPanelRaised.r : kPanel.r,
                           active ? kPanelRaised.g : kPanel.g,
                           active ? kPanelRaised.b : kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &header);
    SDL_SetRenderDrawColor(m_renderer, active ? kAccent.r : kBorder.r,
                           active ? kAccent.g : kBorder.g,
                           active ? kAccent.b : kBorder.b, active ? 230 : 110);
    SDL_RenderDrawRect(m_renderer, &header);
    if (active) {
        SDL_Rect bar{header.x, header.y, 4, header.h};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b,
                               255);
        SDL_RenderFillRect(m_renderer, &bar);
    }
    drawText(truncateText(track.name, fontBold(), header.w - 44), header.x + 12,
             header.y + 5, active ? kText : kTextDim, fontBold());
    addButton("x", header.x + header.w - 22, header.y + 3, 18, 18,
              [this, index] { removeTrack(index); });
    const int buttonY = header.y + 25;
    addButton("M", header.x + 8, buttonY, 24, 18,
              [this, index] {
                  const bool muted = !m_timeline.track(index).muted;
                  m_timeline.setTrackMuted(index, muted);
                  updateMixer();
                  setStatus(muted ? "Track muted" : "Track unmuted");
              },
              false, track.muted);
    addButton("S", header.x + 35, buttonY, 24, 18,
              [this, index] {
                  const bool solo = !m_timeline.track(index).solo;
                  m_timeline.setTrackSolo(index, solo);
                  updateMixer();
                  setStatus(solo ? "Track solo" : "Solo cleared");
              },
              false, track.solo);
    addButton("R", header.x + 62, buttonY, 24, 18,
              [this, index] {
                  const bool armed = !m_timeline.track(index).armed;
                  for (int i = 0; i < m_timeline.trackCount(); ++i)
                      m_timeline.setTrackArmed(i, i == index && armed);
                  setStatus(armed ? "Track armed for recording"
                                  : "Track disarmed");
              },
              false, track.armed);
    addSlider("Vol", formatDb(m_trackControls[static_cast<size_t>(index)].gain.value),
              header.x + 8, header.y + 47, header.w - 16,
              m_trackControls[static_cast<size_t>(index)].gain,
              [this, index](double value) {
                  m_timeline.setTrackGainDb(index, value);
                  updateMixer();
              });
    addSlider("Pan",
              std::to_string(static_cast<int>(std::lround(
                  m_trackControls[static_cast<size_t>(index)].pan.value * 100))) +
                  "%",
              header.x + 8, header.y + 74, header.w - 16,
              m_trackControls[static_cast<size_t>(index)].pan,
              [this, index](double value) {
                  m_timeline.setTrackPan(index, value);
                  updateMixer();
              });
}

void App::ensureOverview() {
    if (!m_overviewDirty && !m_overviewPeaks.empty()) return;
    m_overviewDirty = false;
    m_overviewPeaks.clear();
    const vwave::Audio& audio = activeDocument().audio();
    if (audio.empty()) return;
    m_overviewPeaks = vwave::computePeaks(audio, 0,
                                          {0, audio.numFrames()}, 2048);
}

void App::ensureSpectrogram() {
    if (!m_spectrogramDirty && m_spectrogramTexture) return;
    m_spectrogramDirty = false;
    if (m_spectrogramTexture) {
        SDL_DestroyTexture(m_spectrogramTexture);
        m_spectrogramTexture = nullptr;
    }
    const vwave::Audio& audio = activeDocument().audio();
    if (audio.empty()) return;
    const int64_t frames = audio.numFrames();
    const int hop = static_cast<int>(
        std::max<int64_t>(256, frames / 2048));
    m_spectrogram = vwave::computeSpectrogram(audio, 1024, hop);
    if (m_spectrogram.frameCount <= 0) return;
    const int width = std::min(m_spectrogram.frameCount, 2048);
    const int height = 512;
    std::vector<Uint32> pixels(static_cast<size_t>(width) *
                               static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        const double frequency =
            m_spectralView.frequencyAtY(y + 0.5, height);
        const int bin = std::clamp(
            static_cast<int>(std::llround(
                frequency * m_spectrogram.fftSize /
                std::max(1, m_spectrogram.sampleRate))),
            0, m_spectrogram.binCount - 1);
        for (int x = 0; x < width; ++x) {
            const int frame = static_cast<int>(
                static_cast<double>(x) / std::max(1, width - 1) *
                static_cast<double>(m_spectrogram.frameCount - 1));
            const float db = m_spectrogram.dbAt(frame, bin);
            const float normalized = std::clamp((db + 90.0f) / 90.0f,
                                                0.0f, 1.0f);
            pixels[static_cast<size_t>(y) * width + x] =
                makePixel(normalized, 1.0f);
        }
    }
    m_spectrogramTexture = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!m_spectrogramTexture) return;
    SDL_UpdateTexture(m_spectrogramTexture, nullptr, pixels.data(),
                      width * static_cast<int>(sizeof(Uint32)));
    SDL_SetTextureBlendMode(m_spectrogramTexture, SDL_BLENDMODE_NONE);
    m_spectrogramTextureWidth = width;
    m_spectrogramTextureHeight = height;
}

void App::drawSpectrum(const SDL_Rect& area) {
    ensureSpectrogram();
    m_spectrumArea = {area.x + 10, area.y + 38, area.w - 20, area.h - 74};
    SDL_Rect rect = m_spectrumArea;
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);

    for (int i = 0; i <= 10; ++i) {
        const double frame = m_view.startFrame + m_view.visibleFrames * i / 10.0;
        const int x = rect.x + rect.w * i / 10;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 70);
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        drawText(formatSeconds(frame / std::max(1, m_timeline.sampleRate())),
                 x + 3, area.y + 5, kTextFaint, fontSmall());
    }

    if (!hasTracks() || activeDocument().audio().empty()) {
        drawText("No active track", rect.x + 20, rect.y + 20, kTextDim,
                 fontNormal());
        return;
    }
    if (!m_spectrogramTexture) {
        drawText("No spectral data", rect.x + 20, rect.y + 20, kTextDim,
                 fontNormal());
        return;
    }

    const auto& active = m_timeline.track(m_timeline.activeTrack());
    const vwave::Audio& audio = active.document.audio();
    const int64_t trackStart = active.startFrame;
    const int64_t trackFrames = audio.numFrames();
    const int64_t viewStart = static_cast<int64_t>(std::floor(m_view.startFrame));
    const int64_t viewEnd = static_cast<int64_t>(
        std::ceil(m_view.startFrame + m_view.visibleFrames));
    const int64_t localStart = std::max<int64_t>(0, viewStart - trackStart);
    const int64_t localEnd = std::min<int64_t>(trackFrames,
                                               viewEnd - trackStart);
    if (localEnd > localStart && m_spectrogramTextureWidth > 0 &&
        m_spectrogramTextureHeight > 0) {
        const double timelineStart = static_cast<double>(trackStart + localStart);
        const double timelineEnd = static_cast<double>(trackStart + localEnd);
        const int x0 = rect.x + static_cast<int>(
                                   (timelineStart - m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   rect.w);
        const int x1 = rect.x + static_cast<int>(
                                   (timelineEnd - m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   rect.w);
        SDL_Rect source{
            static_cast<int>(static_cast<double>(localStart) /
                             std::max<int64_t>(1, trackFrames) *
                             m_spectrogramTextureWidth),
            0,
            std::max(1, static_cast<int>(
                            static_cast<double>(localEnd - localStart) /
                            std::max<int64_t>(1, trackFrames) *
                            m_spectrogramTextureWidth)),
            m_spectrogramTextureHeight};
        SDL_Rect destination{x0, rect.y, std::max(1, x1 - x0), rect.h};
        SDL_RenderCopy(m_renderer, m_spectrogramTexture, &source,
                       &destination);
    }

    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);

    const double frequencies[] = {50.0,  100.0,  200.0,  500.0, 1000.0,
                                  2000.0, 5000.0, 10000.0, 20000.0};
    for (double frequency : frequencies) {
        if (frequency < m_spectralView.minHz ||
            frequency > m_spectralView.maxHz)
            continue;
        const double y = m_spectralView.yAtFrequency(frequency, rect.h);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
        SDL_RenderDrawLine(m_renderer, rect.x, rect.y + static_cast<int>(y),
                           rect.x + rect.w, rect.y + static_cast<int>(y));
        std::ostringstream label;
        label << (frequency >= 1000.0 ? frequency / 1000.0 : frequency)
              << (frequency >= 1000.0 ? "k" : "");
        drawText(label.str(), rect.x + 4,
                 rect.y + static_cast<int>(y) - 13, kTextDim, fontSmall());
    }

    if (m_spectralRegion.active && !m_spectralRegion.frames.empty()) {
        const int x0 = rect.x + static_cast<int>(
            (static_cast<double>(trackStart + m_spectralRegion.frames.start) -
             m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * rect.w);
        const int x1 = rect.x + static_cast<int>(
            (static_cast<double>(trackStart + m_spectralRegion.frames.end) -
             m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * rect.w);
        const double highY =
            m_spectralView.yAtFrequency(m_spectralRegion.highHz, rect.h);
        const double lowY =
            m_spectralView.yAtFrequency(m_spectralRegion.lowHz, rect.h);
        SDL_Rect selection{std::min(x0, x1), rect.y + static_cast<int>(highY),
                           std::max(1, std::abs(x1 - x0)),
                           std::max(1, static_cast<int>(lowY - highY))};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 45);
        SDL_RenderFillRect(m_renderer, &selection);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 230);
        SDL_RenderDrawRect(m_renderer, &selection);
        std::ostringstream description;
        description << std::fixed << std::setprecision(0)
                    << m_spectralRegion.lowHz << " - "
                    << m_spectralRegion.highHz << " Hz";
        drawText(description.str(), selection.x + 4, selection.y - 14,
                 kAccent, fontSmall());
    }

    const int x = rect.x + static_cast<int>(
        (static_cast<double>(m_playhead) - m_view.startFrame) /
        std::max(1.0, m_view.visibleFrames) * rect.w);
    if (x >= rect.x && x <= rect.x + rect.w) {
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               255);
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
    }

    drawText("Active: " + active.name + "   " +
                 formatHz(m_spectralView.minHz) + " - " +
                 formatHz(m_spectralView.maxHz),
             rect.x + 4, rect.y + rect.h + 8, kTextFaint, fontSmall());
}

void App::drawWavetable(const SDL_Rect& area) {
    m_wavetableArea = {area.x + 10, area.y + 30, area.w - 20,
                       static_cast<int>(area.h * 0.66)};
    m_frameStripArea = {area.x + 10,
                        m_wavetableArea.y + m_wavetableArea.h + 10,
                        area.w - 20,
                        area.h - m_wavetableArea.h - 50};
    if (!m_wavetable.valid()) {
        drawText("No wavetable loaded", m_wavetableArea.x + 20,
                 m_wavetableArea.y + 20, kTextDim, fontNormal());
        drawText("Select audio and press Extract, or use the CLI/Lua API.",
                 m_wavetableArea.x + 20, m_wavetableArea.y + 44, kTextFaint,
                 fontSmall());
        return;
    }
    m_wavetableView.clamp(m_wavetable.frameCount());
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &m_wavetableArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_wavetableArea);

    const int center = m_wavetableArea.y + m_wavetableArea.h / 2;
    for (int i = 0; i <= 8; ++i) {
        const int x = m_wavetableArea.x + m_wavetableArea.w * i / 8;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 55);
        SDL_RenderDrawLine(m_renderer, x, m_wavetableArea.y, x,
                           m_wavetableArea.y + m_wavetableArea.h);
        const int y = m_wavetableArea.y + m_wavetableArea.h * i / 8;
        SDL_RenderDrawLine(m_renderer, m_wavetableArea.x, y,
                           m_wavetableArea.x + m_wavetableArea.w, y);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 130);
    SDL_RenderDrawLine(m_renderer, m_wavetableArea.x, center,
                       m_wavetableArea.x + m_wavetableArea.w, center);

    const std::vector<float>& frame =
        m_wavetable.frame(m_wavetableView.selectedFrame);
    const int frameSize = static_cast<int>(frame.size());
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 240);
    for (int x = 0; x < m_wavetableArea.w; ++x) {
        const int index = static_cast<int>(
            static_cast<double>(x) / std::max(1, m_wavetableArea.w - 1) *
            static_cast<double>(frameSize - 1));
        const float value = frame[static_cast<size_t>(
            std::clamp(index, 0, frameSize - 1))];
        const int y = center - static_cast<int>(
            value * (m_wavetableArea.h / 2 - 8));
        SDL_RenderDrawLine(m_renderer, m_wavetableArea.x + x, center,
                           m_wavetableArea.x + x, y);
    }

    std::ostringstream info;
    info << "Frame " << (m_wavetableView.selectedFrame + 1) << '/'
         << m_wavetable.frameCount() << "   " << frameSize << " samples   "
         << "Drag to draw";
    drawText(info.str(), m_wavetableArea.x + 8, m_wavetableArea.y + 8,
             kTextDim, fontSmall());

    SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                           kPanelRaised.b, 255);
    SDL_RenderFillRect(m_renderer, &m_frameStripArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_frameStripArea);
    const int cellWidth = std::max(
        2, m_frameStripArea.w / std::max(1, m_wavetable.frameCount()));
    const int stripCenter = m_frameStripArea.y + m_frameStripArea.h / 2;
    for (int frameIndex = 0; frameIndex < m_wavetable.frameCount();
         ++frameIndex) {
        const int x = m_frameStripArea.x + frameIndex * cellWidth;
        if (x >= m_frameStripArea.x + m_frameStripArea.w) break;
        if (frameIndex == m_wavetableView.selectedFrame) {
            SDL_SetRenderDrawColor(m_renderer, kAccentDark.r, kAccentDark.g,
                                   kAccentDark.b, 255);
            SDL_Rect highlight{x, m_frameStripArea.y + 2,
                               std::max(1, cellWidth - 1),
                               m_frameStripArea.h - 4};
            SDL_RenderFillRect(m_renderer, &highlight);
        }
        SDL_SetRenderDrawColor(m_renderer, kTextDim.r, kTextDim.g, kTextDim.b,
                               frameIndex == m_wavetableView.selectedFrame
                                   ? 255
                                   : 130);
        const auto& data = m_wavetable.frame(frameIndex);
        for (int sub = 0; sub < cellWidth && x + sub < m_frameStripArea.x +
                                                       m_frameStripArea.w;
             ++sub) {
            const size_t index = static_cast<size_t>(
                static_cast<double>(sub) / std::max(1, cellWidth) *
                static_cast<double>(data.size() - 1));
            const int y = stripCenter - static_cast<int>(
                data[index] * (m_frameStripArea.h / 2 - 4));
            SDL_RenderDrawPoint(m_renderer, x + sub, y);
        }
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

    const auto drawRow = [&](int row, const std::string& label,
                             const std::string& value,
                             SDL_Rect& previous, SDL_Rect& next) {
        const int y = box.y + 84 + row * 66;
        SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                               kPanelRaised.b, 255);
        SDL_Rect rowRect{box.x + 24, y, box.w - 48, 52};
        SDL_RenderFillRect(m_renderer, &rowRect);
        drawText(label, rowRect.x + 12, rowRect.y + 8, kTextDim, fontSmall());
        drawText(truncateText(value, fontNormal(), rowRect.w - 220),
                 rowRect.x + 12, rowRect.y + 26, kText, fontNormal());
        Button prev;
        prev.rect = {rowRect.x + rowRect.w - 96, rowRect.y + 14, 30, 24};
        prev.label = "<";
        previous = prev.rect;
        drawButton(prev);
        Button nextButton;
        nextButton.rect = {rowRect.x + rowRect.w - 56, rowRect.y + 14, 30, 24};
        nextButton.label = ">";
        next = nextButton.rect;
        drawButton(nextButton);
    };

    const std::string output =
        m_outputDeviceIndex > 0 &&
                m_outputDeviceIndex < static_cast<int>(m_outputDevices.size())
            ? m_outputDevices[static_cast<size_t>(m_outputDeviceIndex)]
            : "System Default";
    const std::string input =
        m_inputDeviceIndex > 0 &&
                m_inputDeviceIndex < static_cast<int>(m_inputDevices.size())
            ? m_inputDevices[static_cast<size_t>(m_inputDeviceIndex)]
            : "System Default";
    drawRow(0, "Audio output", output, m_settingsOutputPrev,
            m_settingsOutputNext);
    drawRow(1, "Audio input", input, m_settingsInputPrev,
            m_settingsInputNext);
    drawRow(2, "Theme", m_settings.theme == "light" ? "Light" : "Dark",
            m_settingsThemePrev, m_settingsThemeNext);

    Button close;
    close.rect = {box.x + box.w - 118, box.y + box.h - 52, 94, 28};
    close.label = "Close";
    close.accent = true;
    m_settingsCloseRect = close.rect;
    drawButton(close);
    drawText(m_settingsPath, box.x + 24, box.y + box.h - 34, kTextFaint,
             fontSmall());
}

void App::drawDialog() {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 170);
    SDL_Rect overlay{0, 0, m_toolbarRect.w, m_footerRect.y + m_footerRect.h};
    SDL_RenderFillRect(m_renderer, &overlay);
    SDL_Rect box{m_toolbarRect.w / 2 - 330, m_footerRect.y / 2 - 95, 660, 190};
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &box);
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 255);
    SDL_RenderDrawRect(m_renderer, &box);
    drawText(m_dialogTitle, box.x + 20, box.y + 18, kText, fontBold());
    drawText("Path", box.x + 20, box.y + 58, kTextDim, fontSmall());
    SDL_Rect field{box.x + 20, box.y + 78, box.w - 40, 34};
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &field);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &field);
    const std::string visiblePath =
        truncateText(m_dialogPath, fontNormal(), field.w - 20);
    drawText(visiblePath, field.x + 8, field.y + 9, kText, fontNormal());
    drawText("Enter to confirm | Esc to cancel | Ctrl+A clears path",
             box.x + 20, box.y + 122, kTextFaint, fontSmall());

    Button confirm;
    confirm.rect = {box.x + box.w - 218, box.y + 150, 94, 28};
    confirm.label = m_dialogSave ? "Save" : "Open";
    if (m_dialogMode == DialogMode::AddTrack) confirm.label = "Add";
    if (m_dialogMode == DialogMode::ExportSlices) confirm.label = "Export";
    confirm.accent = true;
    m_dialogConfirmRect = confirm.rect;
    drawButton(confirm);
    Button cancel;
    cancel.rect = {box.x + box.w - 114, box.y + 150, 94, 28};
    cancel.label = "Cancel";
    m_dialogCancelRect = cancel.rect;
    drawButton(cancel);
}

void App::setTab(vwave::EditorTab tab) {
    if (m_tab != tab) m_sidebarScroll = 0.0;
    m_tab = tab;
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
    vwave::Range range = m_timeline.selection().active
                             ? m_timeline.selectedRange()
                             : vwave::Range{0, m_timeline.lengthFrames()};
    if (range.empty()) {
        setStatus("Nothing to play");
        return;
    }
    if (m_player.play(m_timeline, range.start, range.end)) {
        m_playhead = range.start;
        setStatus("Playing mix");
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
    syncTrackControls();
    m_wavetable = {};
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
    vwave::Timeline timeline;
    std::string error;
    if (!timeline.loadProject(path, &error)) {
        setStatus("Open failed: " + error, 6000);
        return false;
    }
    m_timeline = std::move(timeline);
    m_projectPath = path;
    m_audioPath = hasTracks() ? activeDocument().sourcePath() : std::string{};
    m_wavetable = {};
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
    m_wavetableView.selectedFrame = 0;
    setTab(vwave::EditorTab::Wavetable);
    setStatus("Extracted " + std::to_string(m_wavetable.frameCount()) +
              " wavetable frames");
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
    const vwave::Audio note = vwave::renderWavetableNote(
        m_wavetable, m_synthNote.value, m_synthDuration.value, options);
    if (note.empty()) {
        setStatus("Synth render failed");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.addAudioTrack("Synth", note, 0, &error)) {
        setStatus("Cannot add synth track: " + error);
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
    m_dialogMode = mode;
    m_dialogTitle = title;
    m_dialogPath = initialPath;
    m_dialogSave = save;
    SDL_StartTextInput();
}

void App::confirmDialog() {
    if (m_dialogMode == DialogMode::None) return;
    const DialogMode mode = m_dialogMode;
    const std::string path = m_dialogPath;
    cancelDialog();
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
    case DialogMode::ExportSlices:
        exportSlices(path);
        break;
    case DialogMode::None:
        break;
    }
}

void App::cancelDialog() {
    m_dialogMode = DialogMode::None;
    SDL_StopTextInput();
}

void App::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_QUIT:
        m_running = false;
        break;
    case SDL_DROPFILE: {
        const std::string path = event.drop.file;
        SDL_free(event.drop.file);
        if (m_dialogMode != DialogMode::None) break;
        const std::string extension =
            std::filesystem::path(path).extension().string();
        if (extension == ".vwdoc") loadProject(path);
        else if (m_timeline.empty()) loadAudio(path);
        else addTrackFile(path);
        break;
    }
    case SDL_KEYDOWN:
        handleKey(event.key);
        break;
    case SDL_TEXTINPUT:
        if (m_dialogMode != DialogMode::None)
            m_dialogPath += event.text.text;
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
    if (m_dialogMode != DialogMode::None) {
        if (key == SDLK_ESCAPE) {
            cancelDialog();
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            confirmDialog();
        } else if (key == SDLK_BACKSPACE) {
            if (!m_dialogPath.empty()) {
                m_dialogPath.pop_back();
                while (!m_dialogPath.empty() &&
                       (static_cast<unsigned char>(m_dialogPath.back()) &
                        0xC0) == 0x80)
                    m_dialogPath.pop_back();
            }
        } else if ((event.keysym.mod & KMOD_CTRL) != 0 && key == SDLK_a) {
            m_dialogPath.clear();
        }
        return;
    }
    if (m_settingsOpen) {
        if (key == SDLK_ESCAPE) closeSettings();
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
        openDialog(DialogMode::Open, "Open audio or project", "", false);
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
    } else if (key == SDLK_1) {
        setTab(vwave::EditorTab::Waveform);
    } else if (key == SDLK_2) {
        setTab(vwave::EditorTab::Spectrum);
    } else if (key == SDLK_3) {
        setTab(vwave::EditorTab::Wavetable);
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
    if (m_dialogMode != DialogMode::None) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_dialogConfirmRect)) {
            confirmDialog();
        } else if (SDL_PointInRect(&point, &m_dialogCancelRect)) {
            cancelDialog();
        }
        return;
    }
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
            for (const auto& slider : m_sliders) {
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
            for (const auto& button : m_buttons) {
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
        SDL_Point point{event.x, event.y};
        for (size_t i = 0; i < m_trackHeaderRects.size(); ++i) {
            if (m_trackHeaderRects[i].w > 0 &&
                SDL_PointInRect(&point, &m_trackHeaderRects[i])) {
                selectTrack(static_cast<int>(i));
                return;
            }
        }
        for (size_t i = 0; i < m_trackLaneRects.size(); ++i) {
            if (m_trackLaneRects[i].w > 0 &&
                SDL_PointInRect(&point, &m_trackLaneRects[i])) {
                selectTrack(static_cast<int>(i));
                break;
            }
        }
        if (SDL_PointInRect(&point, &m_overviewArea)) {
            const double fraction =
                static_cast<double>(event.x - m_overviewArea.x) /
                std::max(1, m_overviewArea.w);
            m_view.startFrame =
                fraction * m_timeline.lengthFrames() -
                m_view.visibleFrames / 2.0;
            m_view.clamp(m_timeline.lengthFrames());
            m_panningOverview = true;
            m_lastMouseX = event.x;
            return;
        }
        if (SDL_PointInRect(&point, &m_waveArea)) {
            m_selectionAnchor =
                m_view.frameAt(event.x - m_waveArea.x, m_waveArea.w,
                               m_timeline.lengthFrames());
            m_timeline.setSelection(m_selectionAnchor, m_selectionAnchor);
            m_draggingSelection = true;
        }
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        SDL_Point point{event.x, event.y};
        if (SDL_PointInRect(&point, &m_spectrumArea)) {
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
    }
}

void App::handleMouseUp(const SDL_MouseButtonEvent& event) {
    if (event.button == SDL_BUTTON_MIDDLE) m_panning = false;
    m_draggingSelection = false;
    m_panningOverview = false;
    m_drawingWavetable = false;
    m_lastWavetableSample = -1;
    m_sliderDragging = false;
    if (m_draggingSpectrum) {
        m_draggingSpectrum = false;
        if (m_spectralRegion.frames.length() < 2 ||
            m_spectralRegion.highHz <= m_spectralRegion.lowHz) {
            m_spectralRegion.active = false;
        } else {
            m_spectralLow.setValue(m_spectralRegion.lowHz);
            m_spectralHigh.setValue(m_spectralRegion.highHz);
        }
    }
}

void App::handleMouseMotion(const SDL_MouseMotionEvent& event) {
    if (m_sliderDragging) {
        setSliderFromX(m_activeSlider, event.x);
        return;
    }
    if (m_draggingSelection) {
        const int64_t frame =
            m_view.frameAt(event.x - m_waveArea.x, m_waveArea.w,
                           m_timeline.lengthFrames());
        m_timeline.setSelection(std::min(m_selectionAnchor, frame),
                                std::max(m_selectionAnchor, frame));
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
    if (m_dialogMode != DialogMode::None) return;
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
    if (m_tab == vwave::EditorTab::Spectrum) {
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
