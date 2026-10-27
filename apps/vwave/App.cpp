#include "App.h"

#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"
#include "vwave/Processing.h"
#include "vwave/Recipe.h"
#include "vwave/Synth.h"
#include "vwave/Version.h"

#include <algorithm>
#include <cctype>
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

bool isScopePath(const std::string& path) {
    std::string extension =
        std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return extension == ".vscope";
}

bool isImagePath(const std::string& path) {
    std::string extension =
        std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga" || extension == ".gif" ||
           extension == ".ppm" || extension == ".pgm" ||
           extension == ".hdr";
}
SDL_Color kLane{15, 17, 23, 255};
SDL_Color kLaneActive{20, 25, 30, 255};
SDL_Color kMenu{16, 18, 24, 255};
SDL_Color kDisabled{27, 29, 36, 255};

std::string defaultSettingsPath() {
    std::filesystem::path base;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        base = appdata;
    } else if (const char* profile = std::getenv("USERPROFILE")) {
        base = std::filesystem::path(profile) / "AppData" / "Roaming";
    } else {
        base = std::filesystem::current_path();
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / ".config";
    else
        base = std::filesystem::current_path();
#endif
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

std::string formatTimeInput(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "0.000";
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << seconds;
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

SDL_Color phaseColor(float phase, Uint8 alpha = 255) {
    const double twoPi = 2.0 * 3.14159265358979323846;
    double hue = (static_cast<double>(phase) + 3.14159265358979323846) /
                 twoPi;
    hue -= std::floor(hue);
    const double saturation = 0.78;
    const double value = 0.98;
    const double c = value * saturation;
    const double h = hue * 6.0;
    const double x = c * (1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (h < 1.0) { r = c; g = x; }
    else if (h < 2.0) { r = x; g = c; }
    else if (h < 3.0) { g = c; b = x; }
    else if (h < 4.0) { g = x; b = c; }
    else if (h < 5.0) { r = x; b = c; }
    else { r = c; b = x; }
    const double m = value - c;
    return {static_cast<Uint8>((r + m) * 255.0),
            static_cast<Uint8>((g + m) * 255.0),
            static_cast<Uint8>((b + m) * 255.0), alpha};
}

const char* const kAlgorithmNames[] = {"WSOLA", "Phase Vocoder",
                                       "PaulStretch"};
const int kFrameSizes[] = {256, 512, 1024, 2048, 4096, 8192};
const char* const kSliceModeNames[] = {"Transients", "Beats", "Markers"};
const char* const kMorphModeNames[] = {"Linear", "Equal Power",
                                       "Phase Aligned", "Spectral",
                                       "Harmonic"};
const char* const kInterpolationNames[] = {"Nearest", "Linear", "Cubic"};
const int kWavetableOversampling[] = {1, 2, 4, 8};
const char* const kMasteringProfileNames[] = {
    "Balanced", "Definition", "Separation", "Fullness", "Loudness", "Custom"};
const char* const kImagePhaseNames[] = {"Griffin-Lim", "Fast GL",
                                        "Phase Locked", "Random", "Color"};
const char* const kImageTimeNames[] = {"Direct", "Mirrored", "Looped"};
const char* const kImageFrequencyNames[] = {"Linear", "Log"};
const char* const kScopeShapeNames[] = {"Polyline", "Line", "Circle",
                                        "Ellipse", "Lissajous", "Spiral",
                                        "Figure 8"};
const char* const kScopeDirectionNames[] = {"Forward", "Reverse", "Ping-Pong"};
const int kScopeOversampling[] = {1, 2, 4, 8};

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
    if (m_loopEnabled) {
        const int64_t total = m_timeline.lengthFrames();
        m_loopStart = std::clamp<int64_t>(m_loopStart, 0, total);
        m_loopEnd = std::clamp<int64_t>(m_loopEnd, 0, total);
        if (m_loopEnd <= m_loopStart) {
            m_loopEnabled = false;
            m_loopStart = 0;
            m_loopEnd = 0;
        }
    }
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
               }, m_mastering.valid)};
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
    addButton("Mastering", 454, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Mastering); }, false,
              m_tab == vwave::EditorTab::Mastering);
    addButton("Image", 554, tabY, 76, 29,
              [this] { setTab(vwave::EditorTab::Image); }, false,
              m_tab == vwave::EditorTab::Image);
    addButton("Scope", 634, tabY, 76, 29,
              [this] { setTab(vwave::EditorTab::Oscilloscope); }, false,
              m_tab == vwave::EditorTab::Oscilloscope);

    int x = 720;
    addButton("Open", x, tabY, 60, 29,
              [this] { openDialog(DialogMode::Open, "Open audio, project, or image",
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
    x += 46;
    addButton("|<", x, tabY, 30, 29, [this] { goToStart(); }, false, false,
              !m_timeline.empty());
    x += 34;
    addButton(">|", x, tabY, 30, 29, [this] { goToEnd(); }, false, false,
              !m_timeline.empty());
    x += 34;
    std::ostringstream timeLabel;
    timeLabel << formatSeconds(
        static_cast<double>(m_playhead) /
        std::max(1, m_timeline.sampleRate()));
    addButton(timeLabel.str(), x, tabY, 100, 29, [this] {
        openDialog(
            DialogMode::SeekTime, "Go to time",
            formatTimeInput(static_cast<double>(m_playhead) /
                            std::max(1, m_timeline.sampleRate())),
            false);
    }, false, false, !m_timeline.empty());
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
        grid({{std::string("View: ") + (m_spectrum3D ? "3D" : "2D"),
               [this] {
                   m_spectrum3D = !m_spectrum3D;
                   if (m_spectrum3D) ensurePhaseSpectrogram();
               }},
              {"Fit Frequency", [this] { fitFrequency(); }}});
        grid({{"Freq -", [this] { zoomFrequency(0.5); }},
              {"Freq +", [this] { zoomFrequency(2.0); }}});
        if (m_spectrum3D) {
            drawText("Phase color; alpha = confidence; 3D view is analysis-only.",
                     18, y, kTextFaint, fontSmall());
            y += 18;
        }
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
        syncWavetableDesignControls();
        auto wtGrid = [&](std::initializer_list<std::pair<
                              std::string, std::function<void()>>> items) {
            int column = 0;
            for (const auto& item : items) {
                const int bx = 18 + column * (buttonWidth + gap);
                addButton(item.first, bx, y, buttonWidth, buttonHeight,
                          item.second, false, false, m_wavetable.valid());
                ++column;
                if (column == 2) {
                    column = 0;
                    y += buttonHeight + 6;
                }
            }
            if (column != 0) y += buttonHeight + 6;
        };
        auto wtFullButton = [&](const std::string& label,
                                std::function<void()> action) {
            addButton(label, 18, y, buttonWidth * 2 + gap, buttonHeight,
                      std::move(action), false, false, m_wavetable.valid());
            y += buttonHeight + 6;
        };
        drawSection("WAVETABLE", y);
        grid({{"Size: " + std::to_string(kFrameSizes[m_frameSizeIndex]),
               [this] {
                   m_frameSizeIndex =
                       (m_frameSizeIndex + 1) %
                       static_cast<int>(sizeof(kFrameSizes) /
                                        sizeof(kFrameSizes[0]));
               }},
              {"Extract", [this] { extractWavetable(); }}});
        addButton("Load Wavetable", 18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] {
                      openDialog(DialogMode::LoadWavetable, "Load wavetable",
                                 (std::filesystem::current_path() /
                                  "wavetable.vwt").string(),
                                 false);
                  });
        y += buttonHeight + 6;
        addSlider("Frame count", formatInt(m_wavetableFrameCount.value), 18, y,
                  256, m_wavetableFrameCount);
        y += 30;
        wtGrid({{"Save", [this] {
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
        if (m_wavetable.valid()) {
            drawSection("DESIGN", y);
            wtGrid({{std::string("Morph: ") +
                       kMorphModeNames[static_cast<int>(
                           m_wavetable.morphMode())],
                   [this] { cycleWavetableMorphMode(); }},
                  {std::string("Interp: ") +
                       kInterpolationNames[static_cast<int>(
                           m_wavetable.interpolation())],
                   [this] { cycleWavetableInterpolation(); }}});
            wtGrid({{"Oversample: " + std::to_string(m_wavetable.oversampling()) +
                       "x",
                   [this] { cycleWavetableOversampling(); }},
                  {"Add Frame", [this] { addWavetableFrame(false); }}});
            wtGrid({{"Duplicate", [this] { addWavetableFrame(true); }},
                  {"Delete", [this] { deleteWavetableFrame(); }}});
            addSlider("Harmonics", formatInt(m_harmonicCount.value), 18, y,
                      256, m_harmonicCount);
            y += 30;
            wtFullButton("Add Harmonic Frame",
                         [this] { addHarmonicWavetableFrame(); });
            addSlider("Position", formatMorph(m_framePositionControl.value), 18,
                      y, 256, m_framePositionControl, [this](double value) {
                          if (!m_wavetable.valid()) return;
                          m_wavetable.setFramePosition(
                              m_wavetableView.selectedFrame, value);
                      });
            y += 30;
            addSlider("Table root", formatMidiNote(m_tableRootControl.value), 18,
                      y, 256, m_tableRootControl, [this](double value) {
                          if (m_wavetable.valid())
                              m_wavetable.setRootNote(value);
                      });
            y += 30;
            wtGrid({{"Align Phases", [this] {
                       if (!m_wavetable.valid()) return;
                       m_wavetable.phaseAlignFrames();
                       setStatus("Wavetable frames phase aligned");
                   }},
                  {"Band Limit", [this] {
                       if (!m_wavetable.valid()) return;
                       m_wavetable.bandLimitFrames(
                           static_cast<int>(std::lround(m_harmonicLimit.value)));
                       setStatus("Wavetable band limited");
                   }}});
            addSlider("Harmonic limit", formatInt(m_harmonicLimit.value), 18,
                      y, 256, m_harmonicLimit);
            y += 30;
            addSlider("Quantize bits", formatInt(m_quantizeBits.value), 18, y,
                      256, m_quantizeBits);
            y += 30;
            wtGrid({{"Apply Quantize", [this] {
                       if (!m_wavetable.valid()) return;
                       m_wavetable.quantizeFrames(
                           static_cast<int>(std::lround(m_quantizeBits.value)));
                       setStatus("Wavetable quantized");
                   }},
                  {"Random Phases", [this] {
                       if (!m_wavetable.valid()) return;
                       m_wavetable.randomizePhases(1);
                       setStatus("Wavetable phases randomized");
                   }}});
            drawSection("FREE BLEND", y);
            wtGrid({{std::string("Free Blend: ") +
                       (m_freeBlend ? "On" : "Off"),
                   [this] { m_freeBlend = !m_freeBlend; }},
                  {"Reset Weights", [this] {
                       if (!m_wavetable.valid()) return;
                       m_frameWeights.assign(
                           static_cast<size_t>(m_wavetable.frameCount()), 1.0);
                       syncWavetableDesignControls();
                   }}});
            addSlider("Frame weight", formatMorph(m_frameWeightControl.value),
                      18, y, 256, m_frameWeightControl, [this](double value) {
                          if (!m_wavetable.valid()) return;
                          if (static_cast<int>(m_frameWeights.size()) !=
                              m_wavetable.frameCount())
                              m_frameWeights.assign(
                                  static_cast<size_t>(m_wavetable.frameCount()),
                                  1.0);
                          const int frame = m_wavetableView.selectedFrame;
                          if (frame >= 0 &&
                              frame < static_cast<int>(m_frameWeights.size()))
                              m_frameWeights[static_cast<size_t>(frame)] =
                                  value;
                      });
            y += 30;
        }
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
        wtFullButton("Render Note", [this] { renderWavetableNote(); });
        wtGrid({{"Prev Frame", [this] {
                   if (!m_wavetable.valid()) return;
                   --m_wavetableView.selectedFrame;
                   m_wavetableView.clamp(m_wavetable.frameCount());
                   syncWavetableDesignControls();
               }},
              {"Next Frame", [this] {
                   if (!m_wavetable.valid()) return;
                   ++m_wavetableView.selectedFrame;
                   m_wavetableView.clamp(m_wavetable.frameCount());
                   syncWavetableDesignControls();
               }}});
    } else if (m_tab == vwave::EditorTab::Mastering) {
        drawSection("MASTERING", y);
        addButton(std::string("Profile: ") +
                      kMasteringProfileNames[m_masteringProfile],
                  18, y, buttonWidth * 2 + gap, buttonHeight, [this] {
                      m_masteringProfile =
                          (m_masteringProfile + 1) %
                          static_cast<int>(sizeof(kMasteringProfileNames) /
                                           sizeof(kMasteringProfileNames[0]));
                      m_mastering = {};
                      setStatus("Mastering profile: " +
                                std::string(kMasteringProfileNames[
                                    m_masteringProfile]));
                  });
        y += buttonHeight + 6;
        addButton("Analyze Stems", 18, y, buttonWidth * 2 + gap, 26,
                  [this] { analyzeMasteringStems(); }, true, false,
                  !m_timeline.empty());
        y += 32;
        addSlider("Bands", formatInt(m_masteringBands.value), 18, y, 256,
                  m_masteringBands, [this](double) { m_mastering = {}; });
        y += 30;
        addSlider("Max cut", formatDb(-m_masteringMaxCut.value), 18, y, 256,
                  m_masteringMaxCut, [this](double) { m_mastering = {}; });
        y += 30;
        addSlider("Max boost", formatDb(m_masteringMaxBoost.value), 18, y,
                  256, m_masteringMaxBoost,
                  [this](double) { m_mastering = {}; });
        y += 30;
        addSlider("Smoothing", formatInt(m_masteringSmoothing.value), 18, y,
                  256, m_masteringSmoothing,
                  [this](double) { m_mastering = {}; });
        y += 30;
        addButton(std::string("Include Muted: ") +
                      (m_masteringIncludeMuted ? "On" : "Off"),
                  18, y, buttonWidth * 2 + gap, buttonHeight, [this] {
                      m_masteringIncludeMuted = !m_masteringIncludeMuted;
                      m_mastering = {};
                  });
        y += buttonHeight + 6;
        addSlider("Ceiling", formatDb(m_masteringCeiling.value), 18, y, 256,
                  m_masteringCeiling, [this](double) { m_mastering = {}; });
        y += 30;
        addButton(std::string("Mute originals: ") +
                      (m_masteringMuteOriginals ? "On" : "Off"),
                  18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] { m_masteringMuteOriginals = !m_masteringMuteOriginals; });
        y += buttonHeight + 6;
        addButton("Render Processed Stems", 18, y,
                  buttonWidth * 2 + gap, 26,
                  [this] { renderMasteringStems(); }, true, false,
                  m_mastering.valid);
        y += 32;
        addButton("Export Report", 18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] {
                      openDialog(DialogMode::SaveMasteringReport,
                                 "Export mastering report",
                                 (std::filesystem::current_path() /
                                  "mastering.json").string(),
                                 true);
                  }, false, false, m_mastering.valid);
        y += buttonHeight + 6;
        if (m_mastering.valid) {
            std::ostringstream summary;
            summary << m_mastering.stems.size() << " stems  "
                    << m_mastering.bandCount << " bands";
            drawText(summary.str(), 18, y, kTextDim, fontSmall());
            y += 18;
            std::ostringstream mix;
            mix << "Mix RMS " << formatDb(m_mastering.mixRmsDb)
                << "  TP " << formatDb(m_mastering.mixTruePeakDb);
            drawText(mix.str(), 18, y, kTextDim, fontSmall());
            y += 18;
            std::ostringstream phase;
            phase << "Phase " << std::fixed << std::setprecision(2)
                  << m_mastering.phaseCoherence;
            drawText(phase.str(), 18, y, kTextFaint, fontSmall());
            y += 18;
        }
    } else if (m_tab == vwave::EditorTab::Image) {
        const bool hasImage = m_image.valid();
        auto imageGrid = [&](std::initializer_list<std::pair<
                                 std::string, std::function<void()>>> items) {
            int column = 0;
            for (const auto& item : items) {
                const int bx = 18 + column * (buttonWidth + gap);
                addButton(item.first, bx, y, buttonWidth, buttonHeight,
                          item.second, false, false, hasImage);
                ++column;
                if (column == 2) {
                    column = 0;
                    y += buttonHeight + 6;
                }
            }
            if (column != 0) y += buttonHeight + 6;
        };
        drawSection("IMAGE SPECTROGRAM", y);
        addButton("Load Image", 18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] {
                      openDialog(DialogMode::LoadImage, "Load image",
                                 (std::filesystem::current_path() /
                                  "image.png").string(),
                                 false);
                  });
        y += buttonHeight + 6;
        addButton("Render to Track", 18, y, buttonWidth * 2 + gap, 26,
                  [this] { renderImageToTrack(); }, true, false, hasImage);
        y += 32;
        if (hasImage) {
            std::ostringstream info;
            info << m_image.width << " x " << m_image.height << "   "
                 << truncateText(baseName(m_imagePath), fontSmall(), 180);
            drawText(info.str(), 18, y, kTextDim, fontSmall());
            y += 18;
        }
        addSlider("Duration", formatSecondsControl(m_imageDuration.value), 18,
                  y, 256, m_imageDuration);
        y += 30;
        addSlider("Min frequency", formatHz(m_imageMinHz.value), 18, y, 256,
                  m_imageMinHz);
        y += 30;
        addSlider("Max frequency", formatHz(m_imageMaxHz.value), 18, y, 256,
                  m_imageMaxHz);
        y += 30;
        imageGrid({{std::string("Freq: ") +
                        kImageFrequencyNames[m_imageFrequencyScale],
                    [this] {
                        cycleImageFrequencyScale();
                    }},
                   {std::string("Time: ") +
                        kImageTimeNames[m_imageTimeMapping],
                    [this] { cycleImageTimeMapping(); }}});
        imageGrid({{std::string("Phase: ") +
                        kImagePhaseNames[m_imagePhaseMode],
                    [this] { cycleImagePhaseMode(); }},
                   {"FFT: " + std::to_string(kFrameSizes[m_imageFftIndex]),
                    [this] {
                        m_imageFftIndex =
                            (m_imageFftIndex + 1) %
                            static_cast<int>(sizeof(kFrameSizes) /
                                             sizeof(kFrameSizes[0]));
                    }}});
        imageGrid({{std::string("Channels: ") +
                        (m_imageStereo ? "Stereo" : "Mono"),
                    [this] { m_imageStereo = !m_imageStereo; }},
                   {std::string("Invert: ") +
                        (m_imageInvert ? "On" : "Off"),
                    [this] { m_imageInvert = !m_imageInvert; }}});
        imageGrid({{std::string("Alpha: ") +
                        (m_imageUseAlpha ? "On" : "Off"),
                    [this] { m_imageUseAlpha = !m_imageUseAlpha; }},
                   {std::string("Normalize: ") +
                        (m_imageNormalize ? "On" : "Off"),
                    [this] { m_imageNormalize = !m_imageNormalize; }}});
        imageGrid({{std::string("Flip H: ") +
                        (m_imageFlipHorizontal ? "On" : "Off"),
                    [this] { m_imageFlipHorizontal = !m_imageFlipHorizontal; }},
                   {std::string("Flip V: ") +
                        (m_imageFlipVertical ? "On" : "Off"),
                    [this] { m_imageFlipVertical = !m_imageFlipVertical; }}});
        addSlider("Loops", formatInt(m_imageLoops.value), 18, y, 256,
                  m_imageLoops);
        y += 30;
        addSlider("Gamma", formatMorph(m_imageGamma.value), 18, y, 256,
                  m_imageGamma);
        y += 30;
        addSlider("Contrast", formatMorph(m_imageContrast.value), 18, y, 256,
                  m_imageContrast);
        y += 30;
        addSlider("Threshold", formatMorph(m_imageThreshold.value), 18, y, 256,
                  m_imageThreshold);
        y += 30;
        addSlider("GL iterations", formatInt(m_imageIterations.value), 18, y,
                  256, m_imageIterations);
        y += 30;
    } else if (m_tab == vwave::EditorTab::Oscilloscope) {
        const bool hasScope = m_scopePath.valid();
        drawSection("SCOPE PATH", y);
        addButton(std::string("Shape: ") +
                      kScopeShapeNames[m_scopeShape],
                  18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] { cycleScopeShape(); });
        y += buttonHeight + 6;
        addSlider("Points", formatInt(m_scopePointCount.value), 18, y, 256,
                  m_scopePointCount,
                  [this](double) { generateScopePath(); });
        y += 30;
        addSlider("Size", formatMorph(m_scopeSize.value), 18, y, 256,
                  m_scopeSize, [this](double) { generateScopePath(); });
        y += 30;
        addSlider("A", formatInt(m_scopeA.value), 18, y, 256, m_scopeA,
                  [this](double) { generateScopePath(); });
        y += 30;
        addSlider("B", formatInt(m_scopeB.value), 18, y, 256, m_scopeB,
                  [this](double) { generateScopePath(); });
        y += 30;
        addSlider("Phase", formatMorph(m_scopePhase.value), 18, y, 256,
                  m_scopePhase, [this](double) { generateScopePath(); });
        y += 30;
        addSlider("Turns", formatInt(m_scopeTurns.value), 18, y, 256,
                  m_scopeTurns, [this](double) { generateScopePath(); });
        y += 30;
        addButton(std::string("Closed: ") +
                      (m_scopePath.closed ? "On" : "Off"),
                  18, y, buttonWidth, buttonHeight, [this] {
                      m_scopePath.closed = !m_scopePath.closed;
                  }, false, false, hasScope);
        addButton("Clear Path", 18 + buttonWidth + gap, y, buttonWidth,
                  buttonHeight, [this] {
                      m_scopeShape = 0;
                      m_scopePath = {};
                      m_scopePath.name = "Path";
                      setStatus("Scope path cleared; click the scope to add points");
                  });
        y += buttonHeight + 6;
        addButton("Load Path", 18, y, buttonWidth, buttonHeight, [this] {
            openDialog(DialogMode::LoadScopePath, "Load scope path",
                       (std::filesystem::current_path() /
                        "path.vscope").string(),
                       false);
        });
        addButton("Save Path", 18 + buttonWidth + gap, y, buttonWidth,
                  buttonHeight, [this] {
                      openDialog(DialogMode::SaveScopePath, "Save scope path",
                                 (std::filesystem::current_path() /
                                  "path.vscope").string(),
                                 true);
                  }, false, false, hasScope);
        y += buttonHeight + 6;
        drawSection("RENDER", y);
        addSlider("Scan", formatHz(m_scopeScanHz.value), 18, y, 256,
                  m_scopeScanHz);
        y += 30;
        addSlider("Duration", formatSecondsControl(m_scopeDuration.value), 18,
                  y, 256, m_scopeDuration);
        y += 30;
        addSlider("Gain", formatMorph(m_scopeGain.value), 18, y, 256,
                  m_scopeGain);
        y += 30;
        addSlider("Smoothing", formatMorph(m_scopeSmoothing.value), 18, y, 256,
                  m_scopeSmoothing);
        y += 30;
        addButton(std::string("Band Limit: ") +
                      (m_scopeBandLimited ? "On" : "Off"),
                  18, y, buttonWidth, buttonHeight,
                  [this] { m_scopeBandLimited = !m_scopeBandLimited; });
        addButton("Oversample: " +
                      std::to_string(kScopeOversampling[m_scopeOversampling]) +
                      "x",
                  18 + buttonWidth + gap, y, buttonWidth, buttonHeight,
                  [this] { cycleScopeOversampling(); });
        y += buttonHeight + 6;
        addButton(std::string("Direction: ") +
                      kScopeDirectionNames[m_scopeDirection],
                  18, y, buttonWidth, buttonHeight,
                  [this] { cycleScopeDirection(); });
        addButton(std::string("Normalize: ") +
                      (m_scopeNormalize ? "On" : "Off"),
                  18 + buttonWidth + gap, y, buttonWidth, buttonHeight,
                  [this] { m_scopeNormalize = !m_scopeNormalize; });
        y += buttonHeight + 6;
        addButton("Render to Track", 18, y, buttonWidth * 2 + gap, 26,
                  [this] { renderScopeToTrack(); }, true, false, hasScope);
        y += 32;
        if (hasScope) {
            std::ostringstream info;
            info << m_scopePath.points.size() << " points  "
                 << (m_scopePath.closed ? "closed" : "open") << "  "
                 << formatMorph(m_scopePath.length());
            drawText(info.str(), 18, y, kTextDim, fontSmall());
            y += 18;
        }
    }

    drawSection("TRACK", y);
    addButton(std::string("Selection: ") +
                  (m_timeline.selectionScope() ==
                           vwave::SelectionScope::ActiveTrack
                       ? "Active Track"
                       : "All Tracks"),
              18, y, buttonWidth, buttonHeight,
              [this] { toggleSelectionScope(); }, false, false,
              !m_timeline.empty());
    addButton("Crop Track", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { cropActiveTrack(); }, false, false,
              !m_timeline.empty() && m_timeline.selection().active);
    y += buttonHeight + 6;
    addButton("Shift -1 s", 18, y, buttonWidth, buttonHeight,
              [this] { shiftActiveTrack(-1.0); }, false, false,
              !m_timeline.empty());
    addButton("Shift +1 s", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { shiftActiveTrack(1.0); }, false, false,
              !m_timeline.empty());
    y += buttonHeight + 6;
    const bool hasSelection =
        !m_timeline.empty() && m_timeline.selection().active;
    addButton("Zero-X Cut", 18, y, buttonWidth, buttonHeight,
              [this] { zeroCrossCut(); }, false, false, hasSelection);
    addButton("Snap Zero-X", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { snapActiveSelection(); }, false, false,
              hasSelection);
    y += buttonHeight + 6;
    addButton("Copy to Head", 18, y, buttonWidth, buttonHeight,
              [this] { moveActiveSelectionToPlayhead(true); }, false, false,
              hasSelection);
    addButton("Move to Head", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { moveActiveSelectionToPlayhead(false); },
              false, false, hasSelection);
    y += buttonHeight + 6;
    addButton("Repeat x2", 18, y, buttonWidth, buttonHeight,
              [this] { repeatActiveSelection(2); }, false, false,
              hasSelection);
    addButton("Stretch Track", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { stretchActiveTrack(); }, false, false,
              !m_timeline.empty());
    y += buttonHeight + 6;

    drawSection("LOOP", y);
    addButton(std::string("Loop: ") + (m_loopEnabled ? "On" : "Off"), 18, y,
              buttonWidth, buttonHeight, [this] { toggleLoop(); }, false,
              m_loopEnabled, !m_timeline.empty());
    addButton("Loop Selection", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { setLoopToSelection(); }, false, false,
              hasSelection);
    y += buttonHeight + 6;
    addButton("Clear Loop", 18, y, buttonWidth, buttonHeight,
              [this] { clearLoop(); }, false, false,
              m_loopEnd > m_loopStart);
    y += buttonHeight + 6;

    const int activeIndex = m_timeline.activeTrack();
    const bool activeStereo =
        !m_timeline.empty() && activeDocument().numChannels() == 2;
    const bool activeMono = !m_timeline.empty() &&
                            activeDocument().numChannels() == 1;
    const bool previousMono =
        activeMono && activeIndex > 0 &&
        m_timeline.track(activeIndex - 1).document.numChannels() == 1;
    const bool nextMono =
        activeMono && activeIndex + 1 < m_timeline.trackCount() &&
        m_timeline.track(activeIndex + 1).document.numChannels() == 1;
    drawSection("STEREO", y);
    addButton("Split Stereo", 18, y, buttonWidth, buttonHeight,
              [this] { splitActiveStereoTrack(); }, false, false,
              activeStereo);
    addButton("Pad: " +
                  std::string(vwave::stereoPaddingName(m_stereoPadding)),
              18 + buttonWidth + gap, y, buttonWidth, buttonHeight,
              [this] { cycleStereoPadding(); }, false, false,
              !m_timeline.empty());
    y += buttonHeight + 6;
    addButton("Merge Previous", 18, y, buttonWidth, buttonHeight,
              [this] { combineActiveMono(-1); }, false, false, previousMono);
    addButton("Merge Next", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { combineActiveMono(1); }, false, false,
              nextMono);
    y += buttonHeight + 6;

    const bool hasClips = !m_timeline.empty() && activeDocument().hasClips();
    drawSection("CLIPS", y);
    addButton(hasClips ? "Flatten Clips" : "Make Clips", 18, y, buttonWidth,
              buttonHeight, [this] {
                  if (activeDocument().hasClips())
                      flattenClips();
                  else
                      makeClips();
              }, false, false, !m_timeline.empty());
    addButton(std::string("Clip Tool: ") + (m_clipTool ? "On" : "Off"),
              18 + buttonWidth + gap, y, buttonWidth, buttonHeight,
              [this] { toggleClipTool(); }, false, m_clipTool, hasClips);
    y += buttonHeight + 6;
    addButton("Split Clip", 18, y, buttonWidth, buttonHeight,
              [this] { splitClipAtPlayhead(); }, false, false, hasClips);
    addButton("Delete Clip", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { deleteSelectedClip(); }, false, false,
              hasClips);
    y += buttonHeight + 6;
    addButton("Repeat Clip x2", 18, y, buttonWidth, buttonHeight,
              [this] { repeatSelectedClip(2); }, false, false, hasClips);
    addButton("Move to Head", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { moveSelectedClipToPlayhead(); }, false,
              false, hasClips);
    y += buttonHeight + 6;
    addButton("Stretch Clip", 18, y, buttonWidth, buttonHeight,
              [this] { stretchSelectedClip(); }, false, false, hasClips);
    y += buttonHeight + 6;

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
                                   "Alt+drag shift | 1-6 views | Ctrl+Z undo";
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
            "F9 record | B beats | T slice | L loop | Wheel zoom | 1/2/3 views";
        drawText(right, m_footerRect.w - textWidth(right, fontSmall()) - 14,
                 m_footerRect.y + 7, kTextFaint, fontSmall());
    }
}

int App::laneXForFrame(const SDL_Rect& lane, int64_t frame) const {
    return lane.x + static_cast<int>(std::lround(
                        m_view.pixelAt(frame, std::max(1, lane.w))));
}

int64_t App::laneFrameAtX(const SDL_Rect& lane, int x) const {
    return m_view.frameAt(x - lane.x, std::max(1, lane.w),
                          m_timeline.lengthFrames());
}

SDL_Rect App::selectionScreenRect() const {
    const auto& selection = m_timeline.selection();
    if (!selection.active || m_waveArea.w <= 0) return {};
    SDL_Rect bounds = m_waveArea;
    if (m_timeline.selectionScope() == vwave::SelectionScope::ActiveTrack &&
        m_timeline.activeTrack() >= 0 &&
        m_timeline.activeTrack() <
            static_cast<int>(m_trackLaneRects.size())) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(m_timeline.activeTrack())];
        if (lane.w > 0) bounds = lane;
    }
    const int left = std::clamp(laneXForFrame(bounds, selection.start),
                                bounds.x, bounds.x + bounds.w);
    const int right = std::clamp(laneXForFrame(bounds, selection.end),
                                 bounds.x, bounds.x + bounds.w);
    return {left, bounds.y, std::max(1, right - left), bounds.h};
}

void App::drawWaveform(const SDL_Rect& area) {
    m_trackHeaderRects.clear();
    m_trackLaneRects.clear();
    m_trackHeaderRects.resize(static_cast<size_t>(m_timeline.trackCount()));
    m_trackLaneRects.resize(static_cast<size_t>(m_timeline.trackCount()));
    if (!hasTracks() || !activeDocument().hasClips()) {
        m_clipTool = false;
        m_selectedClip = -1;
    }

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
    m_rulerArea = {laneX, area.y, laneWidth, rulerHeight};
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
    if (m_loopEnd > m_loopStart) {
        const int loopLeft = std::clamp(
            laneXForFrame(m_rulerArea, m_loopStart), m_rulerArea.x,
            m_rulerArea.x + m_rulerArea.w);
        const int loopRight = std::clamp(
            laneXForFrame(m_rulerArea, m_loopEnd), m_rulerArea.x,
            m_rulerArea.x + m_rulerArea.w);
        SDL_Rect bar{loopLeft, area.y + rulerHeight - 5,
                     std::max(1, loopRight - loopLeft), 4};
        SDL_SetRenderDrawColor(
            m_renderer, kOrange.r, kOrange.g, kOrange.b,
            m_loopEnabled ? 230 : 90);
        SDL_RenderFillRect(m_renderer, &bar);
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
    for (int index = 0; index < m_timeline.trackCount(); ++index) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(index)];
        if (lane.w <= 0) continue;
        const auto& track = m_timeline.track(index);
        if (!track.document.hasClips()) continue;
        const auto& clips = track.document.clips();
        for (size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex) {
            const auto& clip = clips[clipIndex];
            const int64_t clipStart = track.startFrame + clip.timelineStart;
            const int64_t clipEnd =
                clipStart + vwave::clipOutputFrames(clip);
            const int x0 = laneXForFrame(lane, clipStart);
            const int x1 = laneXForFrame(lane, clipEnd);
            if (x1 < lane.x || x0 > lane.x + lane.w) continue;
            const int left = std::clamp(x0, lane.x, lane.x + lane.w);
            const int right = std::clamp(x1, lane.x, lane.x + lane.w);
            SDL_Rect box{left, lane.y + 4, std::max(1, right - left),
                         lane.h - 8};
            const bool selected =
                index == m_timeline.activeTrack() &&
                static_cast<int>(clipIndex) == m_selectedClip;
            SDL_SetRenderDrawColor(
                m_renderer, selected ? kAccent.r : kTextDim.r,
                selected ? kAccent.g : kTextDim.g,
                selected ? kAccent.b : kTextDim.b,
                selected ? 45 : 22);
            SDL_RenderFillRect(m_renderer, &box);
            SDL_SetRenderDrawColor(
                m_renderer, selected ? kAccent.r : kBorder.r,
                selected ? kAccent.g : kBorder.g,
                selected ? kAccent.b : kBorder.b,
                selected ? 220 : 120);
            SDL_RenderDrawRect(m_renderer, &box);
            SDL_Rect bar{box.x, box.y, box.w, 3};
            SDL_SetRenderDrawColor(
                m_renderer, selected ? kAccent.r : kTextDim.r,
                selected ? kAccent.g : kTextDim.g,
                selected ? kAccent.b : kTextDim.b,
                selected ? 230 : 90);
            SDL_RenderFillRect(m_renderer, &bar);
            if (box.w > 48) {
                const std::string name =
                    clip.name.empty()
                        ? "Clip " + std::to_string(clipIndex + 1)
                        : clip.name;
                drawText(truncateText(name, fontSmall(), box.w - 8), box.x + 4,
                         box.y + 5, selected ? kText : kTextDim, fontSmall());
            }
        }
        if (index == m_timeline.activeTrack() && m_clipTool &&
            m_selectedClip >= 0 &&
            static_cast<size_t>(m_selectedClip) < clips.size()) {
            const auto& clip = clips[static_cast<size_t>(m_selectedClip)];
            const int64_t clipEnd =
                track.startFrame + clip.timelineStart +
                vwave::clipOutputFrames(clip);
            const int endX = laneXForFrame(lane, clipEnd);
            if (endX >= lane.x && endX <= lane.x + lane.w) {
                SDL_Rect handle{endX - 8, lane.y + 9, 7, 7};
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 240);
                SDL_RenderFillRect(m_renderer, &handle);
            }
        }
    }

    if (m_loopEnd > m_loopStart) {
        const int loopLeft = std::clamp(
            laneXForFrame(m_waveArea, m_loopStart), m_waveArea.x,
            m_waveArea.x + m_waveArea.w);
        const int loopRight = std::clamp(
            laneXForFrame(m_waveArea, m_loopEnd), m_waveArea.x,
            m_waveArea.x + m_waveArea.w);
        SDL_Rect loop{loopLeft, m_waveArea.y,
                      std::max(1, loopRight - loopLeft), m_waveArea.h};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loopEnabled ? 20 : 8);
        SDL_RenderFillRect(m_renderer, &loop);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loopEnabled ? 190 : 80);
        SDL_RenderDrawLine(m_renderer, loopLeft, m_waveArea.y, loopLeft,
                           m_waveArea.y + m_waveArea.h);
        SDL_RenderDrawLine(m_renderer, loopRight, m_waveArea.y, loopRight,
                           m_waveArea.y + m_waveArea.h);
        if (m_loopEnabled) {
            drawText("LOOP", loopLeft + 4, m_waveArea.y + 4, kOrange,
                     fontSmall());
        }
    }

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

    for (int index = 0; index < m_timeline.trackCount(); ++index) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(index)];
        if (lane.w <= 0) continue;
        const auto& track = m_timeline.track(index);
        const int64_t frames = track.document.numFrames();
        if (frames <= 0) continue;
        const int startX = laneXForFrame(lane, track.startFrame);
        const int endX = laneXForFrame(lane, track.startFrame + frames);
        const bool active = index == m_timeline.activeTrack();
        if (startX >= lane.x && startX <= lane.x + lane.w) {
            SDL_Rect handle{startX - 4, lane.y + 2, 8, 14};
            SDL_SetRenderDrawColor(m_renderer, kBlue.r, kBlue.g, kBlue.b,
                                   active ? 220 : 120);
            SDL_RenderFillRect(m_renderer, &handle);
        }
        if (endX >= lane.x && endX <= lane.x + lane.w) {
            SDL_Rect trim{endX - 4, lane.y + lane.h - 16, 8, 14};
            SDL_SetRenderDrawColor(m_renderer, kBlue.r, kBlue.g, kBlue.b,
                                   active ? 220 : 120);
            SDL_RenderFillRect(m_renderer, &trim);
            SDL_Rect stretch{endX - 13, lane.y + 2, 11, 11};
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                   kOrange.b, active ? 230 : 120);
            SDL_RenderFillRect(m_renderer, &stretch);
        }
    }

    if (m_timeline.selection().active) {
        const SDL_Rect selection = selectionScreenRect();
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 50);
        SDL_RenderFillRect(m_renderer, &selection);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 220);
        SDL_RenderDrawLine(m_renderer, selection.x, selection.y, selection.x,
                           selection.y + selection.h);
        SDL_RenderDrawLine(m_renderer, selection.x + selection.w - 1,
                           selection.y, selection.x + selection.w - 1,
                           selection.y + selection.h);
        SDL_Rect startHandle{selection.x - 3, selection.y, 7, selection.h};
        SDL_Rect endHandle{selection.x + selection.w - 4, selection.y, 7,
                           selection.h};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 150);
        SDL_RenderFillRect(m_renderer, &startHandle);
        SDL_RenderFillRect(m_renderer, &endHandle);
        SDL_Rect repeatHandle{selection.x + selection.w - 6,
                              selection.y + selection.h - 12, 12, 12};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               230);
        SDL_RenderFillRect(m_renderer, &repeatHandle);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 150);
        SDL_RenderDrawLine(m_renderer, selection.x,
                           selection.y + selection.h - 1,
                           selection.x + selection.w,
                           selection.y + selection.h - 1);
    }

    if (m_waveDrag != WaveDragMode::None && m_waveDragTrack >= 0 &&
        m_waveDragTrack < static_cast<int>(m_trackLaneRects.size())) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(m_waveDragTrack)];
        if (lane.w > 0) {
            const int x = laneXForFrame(lane, m_waveDragPreviewFrame);
            if (m_waveDrag == WaveDragMode::TrackTrimStart ||
                m_waveDrag == WaveDragMode::TrackTrimEnd) {
                SDL_SetRenderDrawColor(m_renderer, kBlue.r, kBlue.g, kBlue.b,
                                       255);
                SDL_RenderDrawLine(m_renderer, x, lane.y, x,
                                   lane.y + lane.h);
            } else if (m_waveDrag == WaveDragMode::TrackStretch ||
                       m_waveDrag == WaveDragMode::ClipMove ||
                       m_waveDrag == WaveDragMode::ClipStretch) {
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 255);
                SDL_RenderDrawLine(m_renderer, x, lane.y, x,
                                   lane.y + lane.h);
            }
        }
        if (m_waveDrag == WaveDragMode::SelectionRepeat &&
            m_timeline.selection().active && m_repeatDragCount > 0) {
            const SDL_Rect selection = selectionScreenRect();
            for (int repeat = 0; repeat < m_repeatDragCount; ++repeat) {
                SDL_Rect block{selection.x + (repeat + 1) * selection.w,
                               selection.y, selection.w, selection.h};
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 70);
                SDL_RenderFillRect(m_renderer, &block);
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 180);
                SDL_RenderDrawRect(m_renderer, &block);
            }
        }
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

void App::ensurePhaseSpectrogram() {
    if (!m_phaseSpectrogramDirty && m_phaseSpectrogram.valid()) return;
    m_phaseSpectrogramDirty = false;
    const vwave::Audio& audio = activeDocument().audio();
    if (audio.empty()) {
        m_phaseSpectrogram = {};
        return;
    }
    m_phaseSpectrogram =
        vwave::computePhaseSpectrogram(audio, 2048, 1536);
}

void App::drawSpectrum(const SDL_Rect& area) {
    if (m_spectrum3D) {
        drawSpectrum3D(area);
        return;
    }
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

    if (m_loopEnd > m_loopStart) {
        const int loopLeft = std::clamp(
            rect.x + static_cast<int>(
                         (static_cast<double>(m_loopStart) -
                          m_view.startFrame) /
                         std::max(1.0, m_view.visibleFrames) * rect.w),
            rect.x, rect.x + rect.w);
        const int loopRight = std::clamp(
            rect.x + static_cast<int>(
                         (static_cast<double>(m_loopEnd) -
                          m_view.startFrame) /
                         std::max(1.0, m_view.visibleFrames) * rect.w),
            rect.x, rect.x + rect.w);
        SDL_Rect loop{loopLeft, rect.y, std::max(1, loopRight - loopLeft),
                      rect.h};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loopEnabled ? 20 : 8);
        SDL_RenderFillRect(m_renderer, &loop);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loopEnabled ? 190 : 80);
        SDL_RenderDrawLine(m_renderer, loopLeft, rect.y, loopLeft,
                           rect.y + rect.h);
        SDL_RenderDrawLine(m_renderer, loopRight, rect.y, loopRight,
                           rect.y + rect.h);
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

void App::drawSpectrum3D(const SDL_Rect& area) {
    ensurePhaseSpectrogram();
    const int left = area.x + 20;
    const int right = area.x + area.w - 20;
    drawText("Phase-aware 3D spectrum", left, area.y + 8, kAccent,
             fontBold());
    if (!hasTracks() || activeDocument().audio().empty()) {
        drawText("No active track", left, area.y + 54, kTextDim, fontNormal());
        return;
    }
    if (!m_phaseSpectrogram.valid()) {
        drawText("No phase data", left, area.y + 54, kTextDim, fontNormal());
        return;
    }
    const int plotTop = area.y + 38;
    const int plotBottom = area.y + area.h - 54;
    const double plotW = static_cast<double>(right - left);
    const double plotH = static_cast<double>(plotBottom - plotTop);
    const double baseX = left + plotW * 0.03;
    const double baseY = plotTop + plotH * 0.48;
    const double depthX = plotW * 0.15;
    const double depthY = plotH * 0.18;
    const double freqW = plotW * 0.78;
    const double ampH = plotH * 0.40;
    m_spectrumArea = {left, plotTop, static_cast<int>(plotW),
                      static_cast<int>(plotH)};
    SDL_Rect frame{left, plotTop, static_cast<int>(plotW),
                   static_cast<int>(plotH)};
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &frame);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &frame);

    const double maxLimit = std::min(
        20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
    const double minHz = std::clamp(m_spectralView.minHz, 10.0, maxLimit);
    const double maxHz =
        std::clamp(m_spectralView.maxHz, minHz + 1.0, maxLimit);
    const double logRatio = std::log(maxHz / minHz);
    const double floorDb = std::max(
        -120.0, m_phaseSpectrogram.maxDb - 72.0);
    const double spanDb = std::max(1.0, m_phaseSpectrogram.maxDb - floorDb);

    const int timeSlices = std::clamp(
        m_phaseSpectrogram.frameCount, 1, 96);
    const int columns = std::clamp(
        static_cast<int>(plotW / 3.0), 80, 260);
    struct SurfacePoint {
        int x = 0;
        int y = 0;
        SDL_Color color{};
    };
    std::vector<SurfacePoint> previousRow;
    std::vector<SurfacePoint> currentRow;
    std::vector<SDL_Vertex> mesh;
    for (int slice = 0; slice < timeSlices; ++slice) {
        const double depth = timeSlices > 1
                                 ? static_cast<double>(slice) /
                                       static_cast<double>(timeSlices - 1)
                                 : 0.0;
        const int sourceFrame = timeSlices > 1
                                    ? static_cast<int>(std::llround(
                                          depth *
                                          static_cast<double>(
                                              m_phaseSpectrogram.frameCount -
                                              1)))
                                    : 0;
        const double bx = baseX + depth * depthX;
        const double by = baseY + depth * depthY;
        currentRow.resize(static_cast<size_t>(columns));
        for (int column = 0; column < columns; ++column) {
            const double f = columns > 1
                                 ? static_cast<double>(column) /
                                       static_cast<double>(columns - 1)
                                 : 0.0;
            const double frequency = minHz * std::exp(logRatio * f);
            const int bin = std::clamp(
                static_cast<int>(std::llround(
                    frequency * m_phaseSpectrogram.fftSize /
                    std::max(1, m_phaseSpectrogram.sampleRate))),
                0, m_phaseSpectrogram.binCount - 1);
            const float db = m_phaseSpectrogram.dbAt(sourceFrame, bin);
            const float confidence =
                m_phaseSpectrogram.confidenceAt(sourceFrame, bin);
            const float phase =
                m_phaseSpectrogram.phaseAt(sourceFrame, bin);
            const double normalized =
                std::clamp((static_cast<double>(db) - floorDb) / spanDb,
                           0.0, 1.0);
            const double energyScale = std::pow(normalized, 0.65);
            const Uint8 alpha = static_cast<Uint8>(std::clamp(
                (25.0 + 150.0 * confidence) *
                    (0.08 + 0.92 * energyScale) *
                    (0.45 + 0.55 * depth),
                0.0, 235.0));
            SDL_Color color = phaseColor(phase, alpha);
            const double brightness = 0.35 + 0.65 * energyScale;
            color.r = static_cast<Uint8>(color.r * brightness);
            color.g = static_cast<Uint8>(color.g * brightness);
            color.b = static_cast<Uint8>(color.b * brightness);
            currentRow[static_cast<size_t>(column)] = {
                static_cast<int>(std::llround(bx + f * freqW)),
                static_cast<int>(std::llround(by - normalized * ampH)),
                color};
        }
        if (!previousRow.empty()) {
            mesh.clear();
            mesh.reserve(static_cast<size_t>(columns - 1) * 6);
            const Uint8 fillAlpha = static_cast<Uint8>(
                std::clamp(14.0 + 30.0 * depth, 0.0, 60.0));
            const SDL_Color fill{kPanelRaised.r, kPanelRaised.g,
                                 kPanelRaised.b, fillAlpha};
            for (int column = 0; column + 1 < columns; ++column) {
                const auto& a = previousRow[static_cast<size_t>(column)];
                const auto& b = previousRow[static_cast<size_t>(column + 1)];
                const auto& c = currentRow[static_cast<size_t>(column)];
                const auto& d = currentRow[static_cast<size_t>(column + 1)];
                mesh.push_back({SDL_FPoint{static_cast<float>(a.x),
                                           static_cast<float>(a.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(b.x),
                                           static_cast<float>(b.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(c.x),
                                           static_cast<float>(c.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(b.x),
                                           static_cast<float>(b.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(d.x),
                                           static_cast<float>(d.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(c.x),
                                           static_cast<float>(c.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
            }
            if (!mesh.empty())
                SDL_RenderGeometry(m_renderer, nullptr, mesh.data(),
                                   static_cast<int>(mesh.size()), nullptr, 0);
        }
        for (int column = 0; column + 1 < columns; ++column) {
            const auto& a = currentRow[static_cast<size_t>(column)];
            const auto& b = currentRow[static_cast<size_t>(column + 1)];
            if (b.color.a < 20) continue;
            SDL_SetRenderDrawColor(m_renderer, b.color.r, b.color.g,
                                   b.color.b, b.color.a);
            SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
        }
        previousRow = currentRow;
    }

    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 160);
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX),
                       static_cast<int>(baseY),
                       static_cast<int>(baseX + freqW),
                       static_cast<int>(baseY));
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX),
                       static_cast<int>(baseY),
                       static_cast<int>(baseX + depthX),
                       static_cast<int>(baseY + depthY));
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX),
                       static_cast<int>(baseY),
                       static_cast<int>(baseX),
                       static_cast<int>(baseY - ampH));
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX + depthX),
                       static_cast<int>(baseY + depthY),
                       static_cast<int>(baseX + depthX + freqW),
                       static_cast<int>(baseY + depthY));

    if (m_spectrum3DHoverX >= 0 && m_spectrum3DHoverY >= 0) {
        int bestDistance = 24;
        int bestFrame = -1;
        int bestBin = -1;
        int bestX = 0;
        int bestY = 0;
        for (int slice = 0; slice < timeSlices; ++slice) {
            const double depth = timeSlices > 1
                                     ? static_cast<double>(slice) /
                                           static_cast<double>(timeSlices - 1)
                                     : 0.0;
            const int sourceFrame = timeSlices > 1
                                        ? static_cast<int>(std::llround(
                                              depth *
                                              static_cast<double>(
                                                  m_phaseSpectrogram.frameCount -
                                                  1)))
                                        : 0;
            const double bx = baseX + depth * depthX;
            const double by = baseY + depth * depthY;
            for (int column = 0; column < columns; ++column) {
                const double f = columns > 1
                                     ? static_cast<double>(column) /
                                           static_cast<double>(columns - 1)
                                     : 0.0;
                const double frequency = minHz * std::exp(logRatio * f);
                const int bin = std::clamp(
                    static_cast<int>(std::llround(
                        frequency * m_phaseSpectrogram.fftSize /
                        std::max(1, m_phaseSpectrogram.sampleRate))),
                    0, m_phaseSpectrogram.binCount - 1);
                const float db = m_phaseSpectrogram.dbAt(sourceFrame, bin);
                const double normalized =
                    std::clamp((static_cast<double>(db) - floorDb) / spanDb,
                               0.0, 1.0);
                const int x = static_cast<int>(
                    std::llround(bx + f * freqW));
                const int y = static_cast<int>(std::llround(
                    by - normalized * ampH));
                const int dx = x - m_spectrum3DHoverX;
                const int dy = y - m_spectrum3DHoverY;
                const int distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestFrame = sourceFrame;
                    bestBin = bin;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        if (bestFrame >= 0 && bestBin >= 0) {
            SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                   kAccent.b, 255);
            SDL_RenderDrawLine(m_renderer, bestX - 5, bestY, bestX + 5,
                               bestY);
            SDL_RenderDrawLine(m_renderer, bestX, bestY - 5, bestX, bestY + 5);
            const float phase = m_phaseSpectrogram.phaseAt(bestFrame, bestBin);
            const float confidence =
                m_phaseSpectrogram.confidenceAt(bestFrame, bestBin);
            const float db = m_phaseSpectrogram.dbAt(bestFrame, bestBin);
            const double frequency = m_phaseSpectrogram.binFrequency(bestBin);
            const double instantaneous =
                m_phaseSpectrogram.instantaneousHzAt(bestFrame, bestBin);
            const int boxW = 248;
            const int boxH = 64;
            int boxX = std::clamp(m_spectrum3DHoverX + 12,
                                  area.x + 4, area.x + area.w - boxW - 4);
            int boxY = std::clamp(m_spectrum3DHoverY + 12,
                                  area.y + 4, area.y + area.h - boxH - 4);
            SDL_Rect box{boxX, boxY, boxW, boxH};
            SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b,
                                   240);
            SDL_RenderFillRect(m_renderer, &box);
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 255);
            SDL_RenderDrawRect(m_renderer, &box);
            std::ostringstream line1;
            line1 << formatHz(frequency) << "   " << formatDb(db)
                  << "   conf " << std::fixed << std::setprecision(2)
                  << confidence;
            std::ostringstream line2;
            line2 << "phase " << std::fixed << std::setprecision(2)
                  << phase << " rad   inst " << formatHz(instantaneous);
            drawText(line1.str(), boxX + 6, boxY + 5, kText, fontSmall());
            drawText(line2.str(), boxX + 6, boxY + 22, kTextDim, fontSmall());
            drawText("phase color; alpha = confidence", boxX + 6,
                     boxY + 39, kTextFaint, fontSmall());
        }
    }

    const int labelY = static_cast<int>(baseY + depthY + 7);
    const int maxLabelX =
        static_cast<int>(baseX + depthX + freqW);
    const std::string maxLabel = formatHz(maxHz);
    const int maxLabelWidth = textWidth(maxLabel, fontSmall());
    drawText(formatHz(minHz), static_cast<int>(baseX + depthX), labelY,
             kTextFaint, fontSmall());
    int lastLabelRight = static_cast<int>(baseX + depthX) +
                         textWidth(formatHz(minHz), fontSmall());
    for (double label : {100.0, 1000.0, 10000.0}) {
        if (label <= minHz || label >= maxHz) continue;
        const double f = std::log(label / minHz) / logRatio;
        const int x = static_cast<int>(std::llround(
            baseX + depthX + f * freqW));
        const std::string text = formatHz(label);
        const int width = textWidth(text, fontSmall());
        if (x < lastLabelRight + 8 ||
            x + width > maxLabelX - maxLabelWidth - 8)
            continue;
        drawText(text, x, labelY, kTextFaint, fontSmall());
        lastLabelRight = x + width;
    }
    drawText(maxLabel, maxLabelX - maxLabelWidth, labelY, kTextFaint,
             fontSmall());
    drawText("0 dB", left + 2, static_cast<int>(baseY - ampH) - 6,
             kTextFaint, fontSmall());
    drawText("-72 dB", left + 2, static_cast<int>(baseY) - 6, kTextFaint,
             fontSmall());
    drawText("time", static_cast<int>(baseX + depthX * 0.45),
             static_cast<int>(baseY + depthY * 0.55), kTextFaint, fontSmall());

    const int legendX = right - 250;
    const int legendY = area.y + 10;
    drawText("phase", legendX, legendY, kTextFaint, fontSmall());
    const int segments = 48;
    for (int i = 0; i < segments; ++i) {
        const float phase = -static_cast<float>(3.14159265358979323846) +
                            2.0f * static_cast<float>(3.14159265358979323846) *
                                static_cast<float>(i) /
                                static_cast<float>(segments - 1);
        const SDL_Color color = phaseColor(phase);
        SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, 255);
        SDL_Rect segment{legendX + 36 + i * 3, legendY + 1, 3, 10};
        SDL_RenderFillRect(m_renderer, &segment);
    }
    drawText("-pi", legendX + 30, legendY + 14, kTextFaint, fontSmall());
    drawText("+pi", legendX + 36 + segments * 3 - 20, legendY + 14,
             kTextFaint, fontSmall());
    std::ostringstream info;
    info << m_phaseSpectrogram.frameCount << " frames  |  FFT "
         << m_phaseSpectrogram.fftSize << "  |  "
         << m_timeline.track(m_timeline.activeTrack()).name
         << "  |  alpha = confidence";
    drawText(info.str(), left, area.y + 24, kTextFaint, fontSmall());
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
    m_loopEnabled = false;
    m_loopStart = 0;
    m_loopEnd = 0;
    if (wasPlaying) {
        m_player.stop();
        togglePlay();
    }
    setStatus("Loop cleared");
}

void App::toggleLoop() {
    if (m_loopEnabled) {
        clearLoop();
        return;
    }
    const int64_t total = m_timeline.lengthFrames();
    const int64_t start = std::clamp<int64_t>(m_loopStart, 0, total);
    const int64_t end = std::clamp<int64_t>(m_loopEnd, 0, total);
    if (end > start) {
        const bool wasPlaying = m_player.isPlaying();
        m_loopEnabled = true;
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
    if (!m_loopEnabled) return false;
    const int64_t total = m_timeline.lengthFrames();
    const int64_t start = std::clamp<int64_t>(m_loopStart, 0, total);
    const int64_t end = std::clamp<int64_t>(m_loopEnd, 0, total);
    return end > start;
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
    m_loopStart = start;
    m_loopEnd = end;
    m_loopEnabled = true;
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

void App::openTrackContextMenu(int trackIndex, int x, int y) {
    if (trackIndex < 0 || trackIndex >= m_timeline.trackCount()) return;
    selectTrack(trackIndex);
    if (trackIndex < static_cast<int>(m_trackLaneRects.size()) &&
        m_trackLaneRects[static_cast<size_t>(trackIndex)].w > 0) {
        const auto& lane = m_trackLaneRects[static_cast<size_t>(trackIndex)];
        const auto& track = m_timeline.track(trackIndex);
        if (x >= lane.x && x <= lane.x + lane.w &&
            track.document.hasClips()) {
            const int64_t local = std::clamp<int64_t>(
                laneFrameAtX(lane, x) - track.startFrame, 0,
                track.document.numFrames());
            m_selectedClip = track.document.clipIndexAt(local);
        }
    }
    m_contextMenuTrack = trackIndex;
    m_contextMenuPosition = {x, y};
    m_contextMenuItems.clear();
    auto add = [this](std::string label, std::function<void()> action,
                      bool enabled = true) {
        MenuItem item;
        item.label = std::move(label);
        item.action = std::move(action);
        item.enabled = enabled;
        m_contextMenuItems.push_back(std::move(item));
    };
    auto separator = [this] {
        MenuItem item;
        item.separator = true;
        m_contextMenuItems.push_back(std::move(item));
    };
    add(m_timeline.selectionScope() ==
                vwave::SelectionScope::AllTracks
            ? "Select Active Track Only"
            : "Select All Tracks",
        [this] { toggleSelectionScope(); });
    separator();
    const bool hasSelection = m_timeline.selection().active;
    add("Zero-Cross Cut", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        zeroCrossCut();
    }, hasSelection);
    add("Snap Selection to Zero Crossings", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        snapActiveSelection();
    }, hasSelection);
    add("Copy Selection to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        moveActiveSelectionToPlayhead(true);
    }, hasSelection);
    add("Move Selection to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        moveActiveSelectionToPlayhead(false);
    }, hasSelection);
    add("Repeat Selection x2", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        repeatActiveSelection(2);
    }, hasSelection);
    separator();
    add("Crop Track to Selection", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        cropActiveTrack();
    }, hasSelection);
    add("Shift to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        const int64_t start =
            clampFrame(m_playhead, m_timeline.lengthFrames());
        if (m_timeline.setTrackStartFrame(trackIndex, start)) {
            clearAnalysis();
            m_view.clamp(m_timeline.lengthFrames());
            setStatus("Shifted track to playhead");
        }
    });
    add("Shift Left 1 s", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        shiftActiveTrack(-1.0);
    });
    add("Shift Right 1 s", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        shiftActiveTrack(1.0);
    });
    add("Stretch Track In Place", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        stretchActiveTrack();
    }, m_timeline.track(trackIndex).document.numFrames() > 0);
    separator();
    const bool stereo =
        m_timeline.track(trackIndex).document.numChannels() == 2;
    const bool mono =
        m_timeline.track(trackIndex).document.numChannels() == 1;
    add("Split Stereo to Mono Tracks", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        splitActiveStereoTrack();
    }, stereo);
    add("Combine with Previous Mono", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        combineActiveMono(-1);
    }, mono && trackIndex > 0 &&
           m_timeline.track(trackIndex - 1).document.numChannels() == 1);
    add("Combine with Next Mono", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        combineActiveMono(1);
    }, mono && trackIndex + 1 < m_timeline.trackCount() &&
           m_timeline.track(trackIndex + 1).document.numChannels() == 1);
    add("Stereo Padding: " +
            std::string(vwave::stereoPaddingName(m_stereoPadding)),
        [this] { cycleStereoPadding(); });
    separator();
    const bool clipMode =
        m_timeline.track(trackIndex).document.hasClips();
    add(clipMode ? "Flatten Clips" : "Make Clips", [this, trackIndex, clipMode] {
        m_timeline.setActiveTrack(trackIndex);
        if (clipMode)
            flattenClips();
        else
            makeClips();
    }, true);
    add("Clip Tool: On/Off", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        toggleClipTool();
    }, clipMode);
    add("Split Clip at Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        splitClipAtPlayhead();
    }, clipMode);
    add("Delete Clip", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        deleteSelectedClip();
    }, clipMode);
    add("Repeat Clip x2", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        repeatSelectedClip(2);
    }, clipMode);
    add("Move Clip to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        moveSelectedClipToPlayhead();
    }, clipMode);
    add("Stretch Clip", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        stretchSelectedClip();
    }, clipMode);
    separator();
    add("Delete Track", [this, trackIndex] { removeTrack(trackIndex); });
    m_contextMenuOpen = true;
}

void App::closeContextMenu() {
    m_contextMenuOpen = false;
    m_contextMenuItems.clear();
    m_contextMenuItemRects.clear();
    m_contextMenuTrack = -1;
}

void App::drawContextMenu() {
    m_contextMenuItemRects.clear();
    if (!m_contextMenuOpen || m_contextMenuItems.empty()) return;
    int width = 190;
    for (const auto& item : m_contextMenuItems) {
        if (item.separator) continue;
        width = std::max(width, textWidth(item.label, fontNormal()) + 34);
    }
    int height = 8;
    for (const auto& item : m_contextMenuItems)
        height += item.separator ? 7 : 24;
    height += 8;
    int x = m_contextMenuPosition.x;
    int y = m_contextMenuPosition.y;
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(m_window, &windowW, &windowH);
    if (x + width > windowW) x = windowW - width;
    if (y + height > windowH) y = windowH - height;
    x = std::max(0, x);
    y = std::max(0, y);
    m_contextMenuRect = {x, y, width, height};
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_contextMenuRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_contextMenuRect);
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    SDL_Point mouse{mouseX, mouseY};
    int rowY = y + 8;
    m_contextMenuItemRects.resize(m_contextMenuItems.size());
    for (size_t index = 0; index < m_contextMenuItems.size(); ++index) {
        const MenuItem& item = m_contextMenuItems[index];
        if (item.separator) {
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 100);
            SDL_RenderDrawLine(m_renderer, x + 8, rowY + 3,
                               x + width - 8, rowY + 3);
            m_contextMenuItemRects[index] = {};
            rowY += 7;
            continue;
        }
        SDL_Rect row{x + 1, rowY, width - 2, 24};
        m_contextMenuItemRects[index] = row;
        if (item.enabled && SDL_PointInRect(&mouse, &row)) {
            SDL_SetRenderDrawColor(m_renderer, kAccentDark.r, kAccentDark.g,
                                   kAccentDark.b, 255);
            SDL_RenderFillRect(m_renderer, &row);
        }
        drawText(item.label, row.x + 12, row.y + 4,
                 item.enabled ? kText : kTextFaint, fontNormal());
        rowY += 24;
    }
}

void App::drawMastering(const SDL_Rect& area) {
    if (!m_mastering.valid) {
        drawText("No mastering analysis", area.x + 40, area.y + 54,
                 kTextDim, fontTitle());
        drawText("Add aligned stems as tracks, then press Analyze Stems.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        drawText("Profiles offer different spectral-allocation goals.",
                 area.x + 40, area.y + 112, kTextFaint, fontSmall());
        return;
    }
    const int left = area.x + 18;
    const int right = area.x + area.w - 18;
    std::ostringstream summary;
    summary << m_mastering.stems.size() << " stems  |  "
            << kMasteringProfileNames[m_masteringProfile] << "  |  "
            << "Mix RMS " << formatDb(m_mastering.mixRmsDb) << "  |  "
            << "True peak " << formatDb(m_mastering.mixTruePeakDb) << "  |  "
            << "Phase " << std::fixed << std::setprecision(2)
            << m_mastering.phaseCoherence;
    drawText(summary.str(), left, area.y + 10, kTextDim, fontSmall());
    drawText("red = suggested cut    green = suggested boost    top = mix band level",
             right - 520, area.y + 10, kTextFaint, fontSmall());

    const int bandCount = std::max(1, m_mastering.bandCount);
    const int spectrumX = left;
    const int spectrumY = area.y + 36;
    const int spectrumW = right - left;
    const int spectrumH = std::max(90, static_cast<int>(area.h * 0.36));
    SDL_Rect spectrum{spectrumX, spectrumY, spectrumW, spectrumH};
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &spectrum);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &spectrum);
    const double cellWidth =
        static_cast<double>(spectrumW) / static_cast<double>(bandCount);
    for (int band = 0; band < bandCount; ++band) {
        const auto& info = m_mastering.bands[static_cast<size_t>(band)];
        const double normalized =
            std::clamp((info.mixPowerDb + 72.0) / 72.0, 0.0, 1.0);
        const int barHeight = static_cast<int>(
            normalized * static_cast<double>(spectrumH - 12));
        const int x = spectrumX +
                      static_cast<int>(std::floor(band * cellWidth));
        const int nextX = spectrumX + static_cast<int>(
                                           std::floor((band + 1) * cellWidth));
        const int width = std::max(1, nextX - x - 1);
        SDL_Rect bar{x, spectrumY + spectrumH - 6 - barHeight, width,
                     barHeight};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b,
                               170);
        SDL_RenderFillRect(m_renderer, &bar);
        const double masking =
            std::clamp(info.maskingDb / 12.0, 0.0, 1.0);
        SDL_Rect maskBar{x, spectrumY, width,
                         std::max(1, static_cast<int>(masking * 8.0))};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               160);
        SDL_RenderFillRect(m_renderer, &maskBar);
    }
    for (int band = 0; band < bandCount; ++band) {
        const auto& info = m_mastering.bands[static_cast<size_t>(band)];
        const int x = spectrumX +
                      static_cast<int>(std::floor(band * cellWidth));
        const int nextX = spectrumX + static_cast<int>(
                                           std::floor((band + 1) * cellWidth));
        const int width = std::max(1, nextX - x - 1);
        const double coherence = std::clamp(info.phaseCoherence, 0.0, 1.0);
        const int red = static_cast<int>(220.0 * (1.0 - coherence));
        const int green = static_cast<int>(180.0 * coherence + 40.0);
        SDL_SetRenderDrawColor(m_renderer, red, green, 80, 220);
        SDL_RenderDrawLine(m_renderer, x, spectrumY + spectrumH - 4,
                           x + width, spectrumY + spectrumH - 4);
    }
    for (double hz : {100.0, 1000.0, 10000.0}) {
        for (int band = 0; band < bandCount; ++band) {
            const auto& info = m_mastering.bands[static_cast<size_t>(band)];
            if (hz >= info.lowHz && hz < info.highHz) {
                const int x = spectrumX + static_cast<int>(
                                               (band + 0.5) * cellWidth);
                drawText(formatHz(hz), x - 14, spectrumY + spectrumH + 6,
                         kTextFaint, fontSmall());
                break;
            }
        }
    }

    const int rowAreaY = spectrumY + spectrumH + 30;
    const int rowAreaH = area.y + area.h - rowAreaY - 12;
    const int labelWidth = 156;
    const int rowCount = std::max(1, static_cast<int>(m_mastering.stems.size()));
    const int rowHeight =
        std::max(20, std::min(38, rowAreaH / rowCount));
    const int cellX = left + labelWidth;
    const int cellAreaW = right - cellX;
    const double gainCellWidth =
        static_cast<double>(cellAreaW) / static_cast<double>(bandCount);
    for (int stemIndex = 0; stemIndex < rowCount; ++stemIndex) {
        const auto& stem = m_mastering.stems[static_cast<size_t>(stemIndex)];
        const int y = rowAreaY + stemIndex * rowHeight;
        const SDL_Color color = stem.muted ? kTextFaint : kText;
        drawText(truncateText(stem.name, fontSmall(), labelWidth - 12),
                 left, y + 3, color, fontSmall());
        std::ostringstream levels;
        levels << "RMS " << formatDb(stem.rmsDb) << "  TP "
               << formatDb(stem.truePeakDb);
        drawText(levels.str(), left, y + 17, kTextFaint, fontSmall());
        for (int band = 0; band < bandCount; ++band) {
            const auto& info = stem.bands[static_cast<size_t>(band)];
            const int x = cellX + static_cast<int>(
                                        std::floor(band * gainCellWidth));
            const int nextX = cellX + static_cast<int>(
                                           std::floor((band + 1) *
                                                      gainCellWidth));
            SDL_Rect cell{x, y, std::max(1, nextX - x - 1),
                          std::max(10, rowHeight - 4)};
            SDL_Color fill{kPanelRaised.r, kPanelRaised.g, kPanelRaised.b,
                           255};
            if (info.suggestedGainDb < -0.05) {
                const double amount =
                    std::clamp(-info.suggestedGainDb /
                                   std::max(0.001, m_masteringMaxCut.value),
                               0.0, 1.0);
                fill = {static_cast<Uint8>(130 + 100 * amount),
                        static_cast<Uint8>(60 * (1.0 - amount)),
                        static_cast<Uint8>(60 * (1.0 - amount)), 255};
            } else if (info.suggestedGainDb > 0.05) {
                const double amount =
                    std::clamp(info.suggestedGainDb /
                                   std::max(0.001, m_masteringMaxBoost.value),
                               0.0, 1.0);
                fill = {static_cast<Uint8>(50 * (1.0 - amount)),
                        static_cast<Uint8>(110 + 90 * amount),
                        static_cast<Uint8>(60 * (1.0 - amount)), 255};
            }
            SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, 255);
            SDL_RenderFillRect(m_renderer, &cell);
            if (info.dominant) {
                SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                       kAccent.b, 180);
                SDL_RenderDrawRect(m_renderer, &cell);
            }
        }
    }
    drawText("Rows are stems; columns are logarithmic frequency bands.",
             left, area.y + area.h - 10, kTextFaint, fontSmall());
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
    std::string error;
    const vwave::ImageSpectrogramResult generated =
        vwave::imageToSpectrogram(m_image, options, &error);
    if (generated.audio.empty()) {
        setStatus(error.empty() ? "Image render failed" : error, 6000);
        return;
    }
    m_player.stop();
    std::string name = std::filesystem::path(m_imagePath).stem().string();
    if (name.empty()) name = "Image";
    const int64_t start =
        clampFrame(m_playhead, m_timeline.lengthFrames());
    if (!m_timeline.addAudioTrack(name, generated.audio, start, &error)) {
        setStatus("Cannot add image track: " + error);
        return;
    }
    const int trackIndex = m_timeline.trackCount() - 1;
    m_timeline.track(trackIndex).recipe =
        vwave::makeImageTrackRecipe(m_imagePath, options);
    m_timeline.setActiveTrack(trackIndex);
    syncTrackControls();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = start;
    clearAnalysis();
    setTab(vwave::EditorTab::Waveform);
    updateWindowTitle();
    setStatus("Rendered image to track (" +
              formatSeconds(generated.duration) + ")");
}

void App::drawImage(const SDL_Rect& area) {
    if (!m_image.valid()) {
        drawText("No image loaded", area.x + 40, area.y + 54, kTextDim,
                 fontTitle());
        drawText("Load a PNG, JPEG, BMP, TGA, GIF, PPM, PGM, or HDR image.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        drawText("Bright pixels become spectral magnitude; the image maps to time and frequency.",
                 area.x + 40, area.y + 112, kTextFaint, fontSmall());
        return;
    }
    drawText("Image to spectrogram", area.x + 20, area.y + 10, kAccent,
             fontBold());
    std::ostringstream info;
    info << m_image.width << " x " << m_image.height << "   "
         << kImageFrequencyNames[m_imageFrequencyScale] << " frequency   "
         << kImageTimeNames[m_imageTimeMapping] << " time   "
         << kImagePhaseNames[m_imagePhaseMode];
    drawText(info.str(), area.x + 20, area.y + 28, kTextFaint, fontSmall());

    const int availableW = area.w - 40;
    const int availableH = area.h - 72;
    const double scale = std::min(
        static_cast<double>(availableW) / std::max(1, m_image.width),
        static_cast<double>(availableH) / std::max(1, m_image.height));
    const int drawW = std::max(1, static_cast<int>(m_image.width * scale));
    const int drawH = std::max(1, static_cast<int>(m_image.height * scale));
    SDL_Rect target{area.x + (area.w - drawW) / 2,
                    area.y + 46 + (availableH - drawH) / 2, drawW, drawH};
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &target);
    if (m_imageTexture) {
        SDL_RenderCopy(m_renderer, m_imageTexture, nullptr, &target);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &target);
    drawText("time ->", target.x, target.y + target.h + 4, kTextFaint,
             fontSmall());
    drawText("^ high frequency", target.x, target.y - 14, kTextFaint,
             fontSmall());
    drawText("Brightness = magnitude; color can drive phase; alpha gates.",
             area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
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
    std::string error;
    const vwave::Audio rendered =
        vwave::renderScopePath(m_scopePath, options, &error);
    if (rendered.empty()) {
        setStatus(error.empty() ? "Scope render failed" : error, 6000);
        return;
    }
    m_player.stop();
    const int64_t start =
        clampFrame(m_playhead, m_timeline.lengthFrames());
    const std::string name =
        m_scopePath.name.empty() ? std::string("Scope") : m_scopePath.name;
    if (!m_timeline.addAudioTrack(name, rendered, start, &error)) {
        setStatus("Cannot add scope track: " + error);
        return;
    }
    const int trackIndex = m_timeline.trackCount() - 1;
    m_timeline.track(trackIndex).recipe =
        vwave::makeScopeTrackRecipe(m_scopePath, options);
    m_timeline.setActiveTrack(trackIndex);
    syncTrackControls();
    m_view.fit(m_timeline.lengthFrames());
    m_playhead = start;
    clearAnalysis();
    setTab(vwave::EditorTab::Waveform);
    updateWindowTitle();
    setStatus("Rendered scope path to track (" +
              formatSeconds(rendered.duration()) + ")");
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

void App::drawOscilloscope(const SDL_Rect& area) {
    if (!m_scopePath.valid()) {
        drawText("No scope path", area.x + 40, area.y + 54, kTextDim,
                 fontTitle());
        drawText("Choose a generated shape, load a .vscope path, or clear the path and click to draw.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        return;
    }
    drawText("Oscilloscope path", area.x + 20, area.y + 10, kAccent,
             fontBold());
    std::ostringstream info;
    info << m_scopePath.points.size() << " points  "
         << (m_scopePath.closed ? "closed" : "open") << "  "
         << kScopeShapeNames[m_scopeShape] << "  scan "
         << formatHz(m_scopeScanHz.value);
    drawText(info.str(), area.x + 20, area.y + 28, kTextFaint, fontSmall());

    const int availableW = area.w - 40;
    const int availableH = area.h - 78;
    const int side = std::clamp(std::min(availableW, availableH), 160,
                                std::max(160, availableH));
    SDL_Rect rect{area.x + (area.w - side) / 2, area.y + 46, side, side};
    m_scopeArea = rect;
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
    for (int i = 1; i < 8; ++i) {
        const int x = rect.x + rect.w * i / 8;
        const int y = rect.y + rect.h * i / 8;
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        SDL_RenderDrawLine(m_renderer, rect.x, y, rect.x + rect.w, y);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 210);
    SDL_RenderDrawLine(m_renderer, rect.x, rect.y + rect.h / 2,
                       rect.x + rect.w, rect.y + rect.h / 2);
    SDL_RenderDrawLine(m_renderer, rect.x + rect.w / 2, rect.y,
                       rect.x + rect.w / 2, rect.y + rect.h);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);

    auto mapPoint = [&](const vwave::ScopePoint& point) {
        const double x = std::clamp(point.x, -1.0, 1.0);
        const double y = std::clamp(point.y, -1.0, 1.0);
        return SDL_Point{
            rect.x + static_cast<int>(std::llround((x + 1.0) * 0.5 * rect.w)),
            rect.y + static_cast<int>(std::llround((1.0 - y) * 0.5 * rect.h))};
    };
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 240);
    for (size_t i = 1; i < m_scopePath.points.size(); ++i) {
        const SDL_Point a = mapPoint(m_scopePath.points[i - 1]);
        const SDL_Point b = mapPoint(m_scopePath.points[i]);
        SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
    }
    if (m_scopePath.closed && m_scopePath.points.size() > 2) {
        const SDL_Point a = mapPoint(m_scopePath.points.back());
        const SDL_Point b = mapPoint(m_scopePath.points.front());
        SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
    }
    if (m_scopeShape == 0) {
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               230);
        for (const auto& point : m_scopePath.points) {
            const SDL_Point p = mapPoint(point);
            SDL_Rect dot{p.x - 2, p.y - 2, 4, 4};
            SDL_RenderFillRect(m_renderer, &dot);
        }
    }
    drawText("x", rect.x + rect.w + 6, rect.y + rect.h / 2 - 6, kTextFaint,
             fontSmall());
    drawText("y", rect.x + rect.w / 2 - 3, rect.y - 16, kTextFaint,
             fontSmall());
    if (m_scopeShape == 0) {
        drawText("Click to add points; click near the first point to close.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
    } else {
        drawText("Generated path. Choose Polyline to draw by hand.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
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
    const bool seekTime = m_dialogMode == DialogMode::SeekTime;
    drawText(seekTime ? "Time (seconds or MM:SS.mmm)" : "Path",
             box.x + 20, box.y + 58, kTextDim, fontSmall());
    SDL_Rect field{box.x + 20, box.y + 78, box.w - 40, 34};
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &field);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &field);
    const std::string visiblePath =
        truncateText(m_dialogPath, fontNormal(), field.w - 20);
    drawText(visiblePath, field.x + 8, field.y + 9, kText, fontNormal());
    drawText(seekTime
                 ? "Enter to go | Esc to cancel | Ctrl+A clears input"
                 : "Enter to confirm | Esc to cancel | Ctrl+A clears path",
             box.x + 20, box.y + 122, kTextFaint, fontSmall());

    Button confirm;
    confirm.rect = {box.x + box.w - 218, box.y + 150, 94, 28};
    confirm.label = m_dialogSave ? "Save" : "Open";
    if (m_dialogMode == DialogMode::AddTrack) confirm.label = "Add";
    if (m_dialogMode == DialogMode::ExportSlices) confirm.label = "Export";
    if (seekTime) confirm.label = "Go";
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
            m_loopEnd, 0, m_timeline.lengthFrames());
        const int64_t loopStart = std::clamp<int64_t>(
            m_loopStart, 0, m_timeline.lengthFrames());
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
    if (m_loopEnabled) {
        const int64_t total = m_timeline.lengthFrames();
        m_loopStart = std::clamp<int64_t>(m_loopStart, 0, total);
        m_loopEnd = std::clamp<int64_t>(m_loopEnd, 0, total);
        if (m_loopEnd <= m_loopStart) {
            m_loopEnabled = false;
            m_loopStart = 0;
            m_loopEnd = 0;
        }
    }
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
    m_loopEnabled = false;
    m_loopStart = 0;
    m_loopEnd = 0;
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
    m_loopEnabled = false;
    m_loopStart = 0;
    m_loopEnd = 0;
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
                m_loopDragArea = m_rulerArea;
                m_loopAnchor = m_playhead;
                m_loopStart = m_playhead;
                m_loopEnd = m_playhead;
                m_loopEnabled = true;
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
                m_loopDragArea = clipLane;
                m_loopAnchor = frame;
                m_loopStart = frame;
                m_loopEnd = frame;
                m_loopEnabled = true;
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
                m_loopDragArea = m_spectrumArea;
                m_loopAnchor = m_playhead;
                m_loopStart = m_playhead;
                m_loopEnd = m_playhead;
                m_loopEnabled = true;
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
            if (m_loopEnd <= m_loopStart) {
                clearLoop();
            } else {
                m_loopEnabled = true;
                setStatus("Loop " +
                          formatSeconds(static_cast<double>(m_loopStart) /
                                        std::max(1, m_timeline.sampleRate())) +
                          " - " +
                          formatSeconds(static_cast<double>(m_loopEnd) /
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
                m_loopDragArea.w > 0
                    ? laneFrameAtX(m_loopDragArea, event.x)
                    : frame;
            m_loopStart = std::min(m_loopAnchor, loopFrame);
            m_loopEnd = std::max(m_loopAnchor, loopFrame);
            m_loopEnabled = true;
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
