#include "App.h"

#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"
#include "vwave/Processing.h"
#include "vwave/Synth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

const SDL_Color kBackground{14, 15, 19, 255};
const SDL_Color kPanel{23, 25, 32, 255};
const SDL_Color kPanelRaised{31, 34, 43, 255};
const SDL_Color kContent{11, 12, 16, 255};
const SDL_Color kBorder{52, 57, 70, 255};
const SDL_Color kText{218, 222, 234, 255};
const SDL_Color kTextDim{137, 145, 163, 255};
const SDL_Color kTextFaint{88, 95, 112, 255};
const SDL_Color kAccent{76, 201, 176, 255};
const SDL_Color kAccentDark{31, 101, 91, 255};
const SDL_Color kOrange{238, 168, 76, 255};
const SDL_Color kRed{224, 92, 92, 255};
const SDL_Color kBlue{86, 145, 230, 255};

std::string baseName(const std::string& path) {
    if (path.empty()) return "Untitled";
    return std::filesystem::path(path).filename().string();
}

std::string withExtension(std::string path, const std::string& extension) {
    if (path.empty()) return path;
    if (std::filesystem::path(path).extension().empty()) path += extension;
    return path;
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

} // namespace

App::App() = default;

App::~App() {
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
    SDL_SetWindowMinimumSize(m_window, 1000, 680);
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

    m_player.open();
    m_view.fit(1);
    updateWindowTitle();
    setStatus("Drop an audio file or press Ctrl+O", 6000);
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
    m_toolbarRect = {0, 0, width, 56};
    m_footerRect = {0, height - 28, width, 28};
    m_sidebarRect = {0, 56, 292, height - 84};
    m_contentRect = {292, 56, width - 292, height - 84};
}

void App::setStatus(const std::string& text, Uint32 milliseconds) {
    m_status = text;
    m_statusUntil = SDL_GetTicks() + milliseconds;
}

void App::updateWindowTitle() {
    if (!m_window) return;
    std::string name = m_projectPath.empty() ? m_audioPath : m_projectPath;
    if (name.empty()) name = "Untitled";
    const std::string title = "VWaveEditor - " + baseName(name);
    SDL_SetWindowTitle(m_window, title.c_str());
}

void App::draw() {
    layout();
    m_buttons.clear();
    SDL_SetRenderDrawColor(m_renderer, kBackground.r, kBackground.g,
                           kBackground.b, kBackground.a);
    SDL_RenderClear(m_renderer);

    drawToolbar();
    drawSidebar();

    SDL_Rect content = m_contentRect;
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b,
                           kContent.a);
    SDL_RenderFillRect(m_renderer, &content);

    if (m_document.audio().empty()) {
        drawText("Open an audio file to begin", content.x + 40,
                 content.y + 60, kTextDim, fontTitle());
        drawText("Ctrl+O or drag and drop WAV, AIFF, FLAC, OGG",
                 content.x + 40, content.y + 96, kTextFaint, fontNormal());
        drawText("The editor is non-destructive; every edit is undoable.",
                 content.x + 40, content.y + 118, kTextFaint, fontSmall());
    } else if (m_tab == vwave::EditorTab::Waveform) {
        drawWaveform(content);
    } else if (m_tab == vwave::EditorTab::Spectrum) {
        drawSpectrum(content);
    } else {
        drawWavetable(content);
    }

    drawFooter();
    if (m_dialogMode != DialogMode::None) drawDialog();
    SDL_RenderPresent(m_renderer);
}

void App::drawButton(const Button& button) {
    SDL_Color fill = button.accent ? kAccentDark : kPanelRaised;
    if (button.active) fill = kAccentDark;
    if (!button.enabled) fill = SDL_Color{27, 29, 36, 255};
    SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(m_renderer, &button.rect);
    SDL_SetRenderDrawColor(m_renderer,
                           button.enabled ? kBorder.r : kTextFaint.r,
                           button.enabled ? kBorder.g : kTextFaint.g,
                           button.enabled ? kBorder.b : kTextFaint.b, 255);
    SDL_RenderDrawRect(m_renderer, &button.rect);
    const SDL_Color textColor = !button.enabled
                                    ? kTextFaint
                                    : (button.accent ? SDL_Color{194, 255, 238, 255}
                                                     : kText);
    drawTextCentered(button.label, button.rect, textColor, fontSmall());
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

void App::drawToolbar() {
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_toolbarRect);
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 255);
    SDL_Rect accentLine{0, 54, m_toolbarRect.w, 2};
    SDL_RenderFillRect(m_renderer, &accentLine);

    drawText("VWAVE", 18, 13, kAccent, fontTitle());
    drawText("EDITOR", 106, 22, kTextFaint, fontSmall());

    const int tabY = 14;
    addButton("Waveform", 154, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Waveform); }, false,
              m_tab == vwave::EditorTab::Waveform);
    addButton("Spectrum", 254, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Spectrum); }, false,
              m_tab == vwave::EditorTab::Spectrum);
    addButton("Wavetable", 354, tabY, 96, 29,
              [this] { setTab(vwave::EditorTab::Wavetable); }, false,
              m_tab == vwave::EditorTab::Wavetable);

    int x = 472;
    addButton("Open", x, tabY, 68, 29,
              [this] { openDialog(DialogMode::Open, "Open audio or project",
                                  m_audioPath, false); });
    x += 74;
    addButton("Save WAV", x, tabY, 88, 29,
              [this] { openDialog(DialogMode::SaveAudio, "Export audio",
                                  m_audioPath, true); });
    x += 94;
    addButton("Project", x, tabY, 76, 29,
              [this] { openDialog(DialogMode::SaveProject, "Save project",
                                  m_projectPath, true); });
    x += 84;
    addButton(m_player.isPlaying() ? "Playing" : "Play", x, tabY, 70, 29,
              [this] { togglePlay(); }, true,
              m_player.isPlaying(), !m_document.audio().empty());
    x += 76;
    addButton("Stop", x, tabY, 62, 29, [this] { stopPlayback(); },
              false, false, m_player.isPlaying());

    if (m_hasBeats && m_beats.bpm > 0.0) {
        std::ostringstream bpm;
        bpm << std::fixed << std::setprecision(1) << m_beats.bpm << " BPM";
        drawText(bpm.str(), m_toolbarRect.w - 100, 21, kTextDim, fontSmall());
    }
}

void App::drawSidebar() {
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_sidebarRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawLine(m_renderer, m_sidebarRect.w - 1, m_sidebarRect.y,
                       m_sidebarRect.w - 1,
                       m_sidebarRect.y + m_sidebarRect.h);

    SDL_RenderSetClipRect(m_renderer, &m_sidebarRect);
    int y = m_sidebarRect.y + 14;
    drawSection("DOCUMENT", y);
    const std::string name = m_projectPath.empty() ? m_audioPath : m_projectPath;
    drawText(truncateText(baseName(name), fontBold(), 250), 18, y, kText,
             fontBold());
    y += 22;
    {
        std::ostringstream audioInfo;
        const vwave::Audio& audio = m_document.audio();
        audioInfo << formatRate(audio.sampleRate()) << "  "
                  << (audio.numChannels() == 1 ? "mono" : "stereo") << "  "
                  << formatSeconds(audio.duration());
        drawText(audioInfo.str(), 18, y, kTextDim, fontSmall());
    }
    y += 20;
    {
        std::string history = "No edits";
        if (m_document.canUndo())
            history = "Undo: " + m_document.undoLabel();
        drawText(truncateText(history, fontSmall(), 250), 18, y, kTextFaint,
                 fontSmall());
    }
    y += 18;
    {
        std::string selection = "Selection: none";
        if (m_document.selection().active) {
            const vwave::Selection sel = m_document.selection();
            std::ostringstream out;
            out << "Selection: " << formatSeconds(
                       static_cast<double>(sel.start) /
                       std::max(1, m_document.sampleRate()))
                << " - "
                << formatSeconds(static_cast<double>(sel.end) /
                                 std::max(1, m_document.sampleRate()));
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

    const bool hasAudio = !m_document.audio().empty();
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

    drawSection("EDIT", y);
    grid({{"Undo", [this] {
               m_player.stop();
               if (!m_document.undo()) {
                   setStatus("Nothing to undo");
               } else {
                   markEdited();
                   setStatus("Undone");
               }
           }},
          {"Redo", [this] {
               m_player.stop();
               if (!m_document.redo()) {
                   setStatus("Nothing to redo");
               } else {
                   markEdited();
                   setStatus("Redone");
               }
           }},
          {"Normalize", [this] {
               runEdit([this](std::string* error) {
                   return m_document.normalize(-1.0, error);
               }, "Normalized to -1 dBFS");
           }},
          {"Reverse", [this] {
               runEdit([this](std::string* error) {
                   return m_document.reverseSelection(error);
               }, "Reversed");
           }},
          {"Fade In", [this] {
               runEdit([this](std::string* error) {
                   return m_document.fadeIn(error);
               }, "Fade in applied");
           }},
          {"Fade Out", [this] {
               runEdit([this](std::string* error) {
                   return m_document.fadeOut(error);
               }, "Fade out applied");
           }},
          {"Invert", [this] {
               runEdit([this](std::string* error) {
                   return m_document.invertSelection(error);
               }, "Polarity inverted");
           }},
          {"Delete", [this] {
               runEdit([this](std::string* error) {
                   return m_document.deleteSelection(error);
               }, "Selection deleted");
           }},
          {"Crop", [this] {
               runEdit([this](std::string* error) {
                   return m_document.cropToSelection(error);
               }, "Cropped to selection");
           }},
          {"Select All", [this] {
               m_document.selectAll();
               setStatus("Selected everything");
           }},
          {"Clear Sel", [this] {
               m_document.clearSelection();
               setStatus("Selection cleared");
           }},
          {"Copy", [this] {
               if (m_document.copySelection()) setStatus("Copied selection");
               else setStatus("Select audio first");
           }},
          {"Cut", [this] {
               runEdit([this](std::string* error) {
                   return m_document.cutSelection(error);
               }, "Cut selection");
           }},
          {"Paste", [this] {
               runEdit([this](std::string* error) {
                   return m_document.paste(error);
               }, "Pasted");
           }}});

    drawSection("PROCESS", y);
    grid({{"Stretch x2", [this] {
               runEdit([this](std::string* error) {
                   return m_document.timeStretch(
                       2.0, vwave::StretchAlgorithm::Wsola, error);
               }, "Time stretched 2x");
           }},
          {"Pitch +12", [this] {
               runEdit([this](std::string* error) {
                   return m_document.pitchShift(
                       12.0, vwave::StretchAlgorithm::PhaseVocoder, error);
               }, "Pitch shifted +12 semitones");
           }},
          {"Pitch -12", [this] {
               runEdit([this](std::string* error) {
                   return m_document.pitchShift(
                       -12.0, vwave::StretchAlgorithm::PhaseVocoder, error);
               }, "Pitch shifted -12 semitones");
           }},
          {"Paul x4", [this] {
               runEdit([this](std::string* error) {
                   return m_document.timeStretch(
                       4.0, vwave::StretchAlgorithm::PaulStretch, error);
               }, "PaulStretch 4x");
           }}});

    drawSection("ANALYZE", y);
    grid({{"Beats", [this] { analyzeBeats(); }},
          {"Pitch", [this] { detectPitch(); }},
          {"Slice", [this] { sliceTransients(); }},
          {"Export", [this] {
               openDialog(DialogMode::ExportSlices,
                          "Export slices to directory",
                          (std::filesystem::current_path() / "slices").string(),
                          true);
           }}});

    if (m_tab == vwave::EditorTab::Spectrum) {
        drawSection("SPECTRAL", y);
        grid({{"+6 dB", [this] { applySpectralGain(6.0); }},
              {"-6 dB", [this] { applySpectralGain(-6.0); }},
              {"Erase", [this] { applySpectralGain(-120.0); }},
              {"Gate -40", [this] { applySpectralGate(); }},
              {"Lowpass", [this] { applySpectralFilter(true); }},
              {"Highpass", [this] { applySpectralFilter(false); }}});
    } else if (m_tab == vwave::EditorTab::Wavetable) {
        drawSection("WAVETABLE", y);
        grid({{"Extract", [this] { extractWavetable(); }},
              {"Save", [this] {
                   openDialog(DialogMode::SaveWavetable, "Save wavetable",
                              (std::filesystem::current_path() /
                               "wavetable.vwt").string(),
                              true);
               }},
              {"Prev", [this] {
                   if (!m_wavetable.valid()) return;
                   --m_wavetableView.selectedFrame;
                   m_wavetableView.clamp(m_wavetable.frameCount());
               }},
              {"Next", [this] {
                   if (!m_wavetable.valid()) return;
                   ++m_wavetableView.selectedFrame;
                   m_wavetableView.clamp(m_wavetable.frameCount());
               }},
              {"Normalize", [this] {
                   if (!m_wavetable.valid()) return;
                   m_wavetable.normalizeFrames();
                   setStatus("Wavetable normalized");
               }},
              {"Render", [this] { renderWavetableNote(); }}});
    }
    SDL_RenderSetClipRect(m_renderer, nullptr);
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
    const std::string right = "B beats | P pitch | T slice | M marker | "
                              "W extract table";
    drawText(right, m_footerRect.w - textWidth(right, fontSmall()) - 14,
             m_footerRect.y + 7, kTextFaint, fontSmall());
}

void App::drawWaveform(const SDL_Rect& area) {
    const vwave::Audio& audio = m_document.audio();
    const int rulerHeight = 24;
    const int overviewHeight = 30;
    m_overviewArea = {area.x + 10, area.y + 6, area.w - 20, overviewHeight - 10};
    SDL_Rect header{area.x + 10, area.y + 3, area.w - 20, rulerHeight};
    SDL_Rect wave{area.x + 10, area.y + rulerHeight + overviewHeight,
                  area.w - 20,
                  area.h - rulerHeight - overviewHeight - 12};
    m_waveArea = wave;

    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &header);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &wave);

    const double totalFrames = std::max(1.0, static_cast<double>(audio.numFrames()));
    const int ticks = 10;
    for (int i = 0; i <= ticks; ++i) {
        const double frame = m_view.startFrame +
                             m_view.visibleFrames * i / ticks;
        const int x = wave.x + wave.w * i / ticks;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
        SDL_RenderDrawLine(m_renderer, x, wave.y, x, wave.y + wave.h);
        const double seconds = frame / audio.sampleRate();
        drawText(formatSeconds(seconds), x + 3, area.y + 7, kTextFaint,
                 fontSmall());
    }

    SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                           kPanelRaised.b, 255);
    SDL_RenderFillRect(m_renderer, &m_overviewArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_overviewArea);
    ensureOverview();
    if (!m_overviewPeaks.empty()) {
        const int center = m_overviewArea.y + m_overviewArea.h / 2;
        SDL_SetRenderDrawColor(m_renderer, kTextDim.r, kTextDim.g, kTextDim.b,
                               110);
        SDL_RenderDrawLine(m_renderer, m_overviewArea.x, center,
                           m_overviewArea.x + m_overviewArea.w, center);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 150);
        for (int x = 0; x < m_overviewArea.w; ++x) {
            const size_t index = static_cast<size_t>(
                static_cast<double>(x) / m_overviewArea.w *
                static_cast<double>(m_overviewPeaks.size()));
            if (index >= m_overviewPeaks.size()) continue;
            const float minimum = m_overviewPeaks[index].minimum;
            const float maximum = m_overviewPeaks[index].maximum;
            const int y0 = center - static_cast<int>(maximum *
                                                     (m_overviewArea.h / 2 - 2));
            const int y1 = center - static_cast<int>(minimum *
                                                     (m_overviewArea.h / 2 - 2));
            SDL_RenderDrawLine(m_renderer, m_overviewArea.x + x, y0,
                               m_overviewArea.x + x, y1);
        }
        const double viewStart = m_view.startFrame / totalFrames;
        const double viewEnd = (m_view.startFrame + m_view.visibleFrames) /
                               totalFrames;
        SDL_Rect viewBox{
            m_overviewArea.x + static_cast<int>(viewStart * m_overviewArea.w),
            m_overviewArea.y + 2,
            std::max(2, static_cast<int>((viewEnd - viewStart) *
                                         m_overviewArea.w)),
            m_overviewArea.h - 4};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b, 220);
        SDL_RenderDrawRect(m_renderer, &viewBox);
    }
    if (m_document.selection().active) {
        const double start = static_cast<double>(m_document.selection().start) /
                             totalFrames;
        const double end = static_cast<double>(m_document.selection().end) /
                           totalFrames;
        SDL_Rect selection{
            m_overviewArea.x + static_cast<int>(start * m_overviewArea.w),
            m_overviewArea.y + 1,
            std::max(1, static_cast<int>((end - start) * m_overviewArea.w)),
            m_overviewArea.h - 2};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 90);
        SDL_RenderFillRect(m_renderer, &selection);
    }

    const int laneCount =
        std::min(2, std::max(1, audio.numChannels()));
    const int laneGap = 4;
    const int laneHeight = (wave.h - (laneCount - 1) * laneGap) / laneCount;
    for (int ch = 0; ch < laneCount; ++ch) {
        SDL_Rect lane{wave.x, wave.y + ch * (laneHeight + laneGap), wave.w,
                      laneHeight};
        drawWaveformChannel(lane, ch, audio, m_view.startFrame,
                            m_view.visibleFrames, ch == 0);
    }

    const double viewEndFrame = m_view.startFrame + m_view.visibleFrames;
    if (m_hasBeats) {
        for (size_t i = 0; i < m_beats.beats.size(); ++i) {
            const int64_t beat = m_beats.beats[i];
            if (beat < m_view.startFrame || beat > viewEndFrame) continue;
            const int x = wave.x + static_cast<int>(
                (static_cast<double>(beat) - m_view.startFrame) /
                std::max(1.0, m_view.visibleFrames) * wave.w);
            SDL_SetRenderDrawColor(m_renderer,
                                   (i % 4 == 0) ? kBlue.r : kBorder.r,
                                   (i % 4 == 0) ? kBlue.g : kBorder.g,
                                   (i % 4 == 0) ? kBlue.b : kBorder.b,
                                   (i % 4 == 0) ? 170 : 100);
            SDL_RenderDrawLine(m_renderer, x, wave.y, x, wave.y + wave.h);
        }
    }
    for (const auto& slice : m_document.slices()) {
        const int64_t frame = slice.range.start;
        if (frame < m_view.startFrame || frame > viewEndFrame) continue;
        const int x = wave.x + static_cast<int>(
            (static_cast<double>(frame) - m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * wave.w);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 190);
        SDL_RenderDrawLine(m_renderer, x, wave.y, x, wave.y + wave.h);
    }
    for (const auto& marker : m_document.markers()) {
        if (marker.frame < m_view.startFrame || marker.frame > viewEndFrame)
            continue;
        const int x = wave.x + static_cast<int>(
            (static_cast<double>(marker.frame) - m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * wave.w);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b, 220);
        SDL_RenderDrawLine(m_renderer, x, wave.y, x, wave.y + wave.h);
        drawText(marker.name, x + 3, wave.y + 3, kOrange, fontSmall());
    }

    if (m_document.selection().active) {
        const double start = static_cast<double>(m_document.selection().start);
        const double end = static_cast<double>(m_document.selection().end);
        const int x0 = wave.x + static_cast<int>(
            (start - m_view.startFrame) / std::max(1.0, m_view.visibleFrames) *
            wave.w);
        const int x1 = wave.x + static_cast<int>(
            (end - m_view.startFrame) / std::max(1.0, m_view.visibleFrames) *
            wave.w);
        const int left = std::clamp(std::min(x0, x1), wave.x, wave.x + wave.w);
        const int right = std::clamp(std::max(x0, x1), wave.x, wave.x + wave.w);
        SDL_Rect selection{left, wave.y, std::max(1, right - left), wave.h};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 55);
        SDL_RenderFillRect(m_renderer, &selection);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 220);
        SDL_RenderDrawLine(m_renderer, left, wave.y, left, wave.y + wave.h);
        SDL_RenderDrawLine(m_renderer, right, wave.y, right, wave.y + wave.h);
    }

    if (m_player.isPlaying() || m_playhead >= m_view.startFrame) {
        if (m_playhead >= m_view.startFrame &&
            m_playhead <= viewEndFrame) {
            const int x = wave.x + static_cast<int>(
                (static_cast<double>(m_playhead) - m_view.startFrame) /
                std::max(1.0, m_view.visibleFrames) * wave.w);
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                                   255);
            SDL_RenderDrawLine(m_renderer, x, wave.y, x, wave.y + wave.h);
        }
    }
    if (m_document.selection().active) {
        drawText(formatSeconds(static_cast<double>(
                                   m_document.selection().end -
                                   m_document.selection().start) /
                               std::max(1, m_document.sampleRate())) +
                     " selected",
                 wave.x + 8, wave.y + wave.h - 18, kTextDim, fontSmall());
    }
}

void App::drawWaveformChannel(const SDL_Rect& lane, int channel,
                              const vwave::Audio& audio, double startFrame,
                              double visibleFrames, bool) {
    SDL_SetRenderDrawColor(m_renderer, 16, 18, 24, 255);
    SDL_RenderFillRect(m_renderer, &lane);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
    SDL_RenderDrawLine(m_renderer, lane.x, lane.y + lane.h / 2,
                       lane.x + lane.w, lane.y + lane.h / 2);

    const int64_t start = static_cast<int64_t>(std::floor(startFrame));
    const int64_t end = std::min<int64_t>(
        audio.numFrames(),
        static_cast<int64_t>(std::ceil(startFrame + visibleFrames)));
    if (start >= end) return;
    const std::vector<vwave::PeakBin> peaks =
        vwave::computePeaks(audio, channel, {start, end}, lane.w);
    const int center = lane.y + lane.h / 2;
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 220);
    for (int i = 0; i < static_cast<int>(peaks.size()); ++i) {
        const float minimum = peaks[static_cast<size_t>(i)].minimum;
        const float maximum = peaks[static_cast<size_t>(i)].maximum;
        int y0 = center - static_cast<int>(maximum * (lane.h / 2 - 4));
        int y1 = center - static_cast<int>(minimum * (lane.h / 2 - 4));
        if (y0 == y1) {
            --y0;
            ++y1;
        }
        SDL_RenderDrawLine(m_renderer, lane.x + i, y0, lane.x + i, y1);
    }
}

void App::ensureOverview() {
    if (!m_overviewDirty && !m_overviewPeaks.empty()) return;
    m_overviewDirty = false;
    m_overviewPeaks.clear();
    const vwave::Audio& audio = m_document.audio();
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
    const vwave::Audio& audio = m_document.audio();
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
    m_spectrumArea = {area.x + 10, area.y + 24, area.w - 20, area.h - 60};
    SDL_Rect rect = m_spectrumArea;
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);
    if (!m_spectrogramTexture) {
        drawText("No spectral data", rect.x + 20, rect.y + 20, kTextDim,
                 fontNormal());
        return;
    }
    SDL_RenderCopy(m_renderer, m_spectrogramTexture, nullptr, &rect);

    const double frequencies[] = {50.0,  100.0,  200.0,  500.0, 1000.0,
                                  2000.0, 5000.0, 10000.0, 20000.0};
    for (double frequency : frequencies) {
        if (frequency < m_spectralView.minHz ||
            frequency > m_spectralView.maxHz)
            continue;
        const double y = m_spectralView.yAtFrequency(frequency, rect.h);
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 35);
        SDL_RenderDrawLine(m_renderer, rect.x, rect.y + static_cast<int>(y),
                           rect.x + rect.w, rect.y + static_cast<int>(y));
        std::ostringstream label;
        label << (frequency >= 1000.0 ? frequency / 1000.0 : frequency)
              << (frequency >= 1000.0 ? "k" : "");
        drawText(label.str(), rect.x + 4,
                 rect.y + static_cast<int>(y) - 13, kTextDim, fontSmall());
    }

    if (m_spectralRegion.active && !m_spectralRegion.frames.empty()) {
        const double totalFrames = std::max<double>(
            1.0, static_cast<double>(m_document.numFrames()));
        const int x0 = rect.x + static_cast<int>(
            static_cast<double>(m_spectralRegion.frames.start) / totalFrames *
            rect.w);
        const int x1 = rect.x + static_cast<int>(
            static_cast<double>(m_spectralRegion.frames.end) / totalFrames *
            rect.w);
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

    if (m_playhead >= 0 && m_document.numFrames() > 0) {
        const int x = rect.x + static_cast<int>(
            static_cast<double>(m_playhead) / m_document.numFrames() * rect.w);
        if (x >= rect.x && x <= rect.x + rect.w) {
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                                   255);
            SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        }
    }
    drawText("Drag a time-frequency region, then use Spectral controls",
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
    SDL_SetRenderDrawColor(m_renderer, 16, 18, 24, 255);
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
    drawText("Enter to confirm | Esc to cancel", box.x + 20, box.y + 122,
             kTextFaint, fontSmall());

    Button confirm;
    confirm.rect = {box.x + box.w - 218, box.y + 150, 94, 28};
    confirm.label = m_dialogSave ? "Save" : "Open";
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
    m_tab = tab;
    if (tab == vwave::EditorTab::Spectrum) m_spectrogramDirty = true;
}

void App::togglePlay() {
    if (m_player.isPlaying()) {
        stopPlayback();
        return;
    }
    if (m_document.audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    vwave::Range range = m_document.selection().active
                             ? m_document.selectedRange()
                             : vwave::Range{0, m_document.numFrames()};
    if (range.empty()) {
        setStatus("Nothing to play");
        return;
    }
    if (m_player.play(m_document.audio(), range.start, range.end)) {
        m_playhead = range.start;
        setStatus("Playing");
    } else {
        setStatus("Audio playback is unavailable");
    }
}

void App::stopPlayback() {
    m_player.stop();
    if (m_player.sourceStart() <= m_document.numFrames())
        m_playhead = m_player.sourceStart();
}

void App::updatePlayback() {
    if (!m_player.isPlaying()) return;
    m_playhead = clampFrame(m_player.sourcePosition(),
                            m_document.numFrames());
    const double viewEnd = m_view.startFrame + m_view.visibleFrames;
    if (m_playhead < m_view.startFrame || m_playhead > viewEnd) {
        m_view.startFrame =
            static_cast<double>(m_playhead) - m_view.visibleFrames * 0.15;
        m_view.clamp(m_document.numFrames());
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
    m_view.clamp(m_document.numFrames());
    m_playhead = clampFrame(m_playhead, m_document.numFrames());
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
    if (m_document.audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    setStatus("Analyzing beats...", 60000);
    m_beats = vwave::analyzeBeats(m_document.audio());
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
    if (m_document.audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    const vwave::Range range = m_document.selection().active
                                   ? m_document.selectedRange()
                                   : vwave::Range{};
    m_pitch = vwave::detectPitch(m_document.audio(), range);
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

void App::sliceTransients() {
    if (m_document.audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    std::vector<vwave::Slice> slices =
        vwave::sliceByTransients(m_document.audio(), 0.5, 0.03);
    if (slices.empty()) {
        setStatus("No slices found");
        return;
    }
    m_document.setSlices(std::move(slices));
    setStatus("Created " + std::to_string(m_document.slices().size()) +
              " slices");
}

void App::addMarkerAtPlayhead() {
    if (m_document.audio().empty()) return;
    const int64_t frame = clampFrame(m_playhead, m_document.numFrames());
    if (m_document.addMarker(frame, "Marker")) {
        setStatus("Marker added at " + formatSeconds(
                                       static_cast<double>(frame) /
                                       std::max(1, m_document.sampleRate())));
    }
}

bool App::loadFile(const std::string& path) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".vwdoc") return loadProject(path);
    return loadAudio(path);
}

bool App::loadAudio(const std::string& path) {
    m_player.stop();
    std::string error;
    if (!m_document.loadAudio(path, &error)) {
        setStatus("Open failed: " + error, 6000);
        return false;
    }
    m_audioPath = path;
    m_projectPath.clear();
    m_wavetable = {};
    m_wavetableView.selectedFrame = 0;
    m_view.fit(m_document.numFrames());
    m_spectralView.maxHz = std::min(
        20000.0, std::max(1000.0, m_document.sampleRate() / 2.0));
    clearAnalysis();
    m_playhead = 0;
    updateWindowTitle();
    setStatus("Loaded " + baseName(path));
    return true;
}

bool App::loadProject(const std::string& path) {
    m_player.stop();
    std::string error;
    if (!m_document.loadProject(path, &error)) {
        setStatus("Open failed: " + error, 6000);
        return false;
    }
    m_projectPath = path;
    m_audioPath = m_document.sourcePath();
    m_wavetable = {};
    m_wavetableView.selectedFrame = 0;
    m_view.fit(m_document.numFrames());
    m_spectralView.maxHz = std::min(
        20000.0, std::max(1000.0, m_document.sampleRate() / 2.0));
    clearAnalysis();
    m_playhead = 0;
    updateWindowTitle();
    setStatus("Loaded project " + baseName(path));
    return true;
}

void App::saveAudio(const std::string& path) {
    const std::string target = withExtension(path, ".wav");
    std::string error;
    if (!m_document.saveAudio(target, {}, &error)) {
        setStatus("Save failed: " + error, 6000);
        return;
    }
    m_audioPath = target;
    m_projectPath.clear();
    updateWindowTitle();
    setStatus("Saved " + baseName(target));
}

void App::saveProject(const std::string& path) {
    const std::string target = withExtension(path, ".vwdoc");
    std::string error;
    if (!m_document.saveProject(target, &error)) {
        setStatus("Save failed: " + error, 6000);
        return;
    }
    m_projectPath = target;
    updateWindowTitle();
    setStatus("Saved project " + baseName(target));
}

void App::exportSlices(const std::string& directory) {
    if (m_document.slices().empty()) {
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
    if (!vwave::exportSlices(m_document.audio(), m_document.slices(),
                             directory, options, &result, &error)) {
        setStatus("Export failed: " + error, 6000);
        return;
    }
    setStatus("Exported " + std::to_string(result.files.size()) + " slices");
}

void App::extractWavetable() {
    if (m_document.audio().empty()) {
        setStatus("Open an audio file first");
        return;
    }
    vwave::WavetableExtractOptions options;
    options.frameSize = 2048;
    options.frameCount = 8;
    if (m_document.selection().active)
        options.range = m_document.selectedRange();
    options.name = baseName(m_audioPath);
    std::string error;
    m_wavetable = vwave::extractWavetable(m_document.audio(), options,
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
        success = m_wavetable.saveWav(path, m_document.sampleRate(), &error);
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
    options.rootNote = 60.0;
    options.gain = 0.7;
    const vwave::Audio note =
        vwave::renderWavetableNote(m_wavetable, 60.0, 2.0, options);
    if (note.empty()) {
        setStatus("Synth render failed");
        return;
    }
    m_player.stop();
    m_document = vwave::Document::fromAudio(note);
    m_audioPath.clear();
    m_projectPath.clear();
    m_view.fit(m_document.numFrames());
    m_playhead = 0;
    m_tab = vwave::EditorTab::Waveform;
    clearAnalysis();
    updateWindowTitle();
    setStatus("Rendered wavetable note");
}

void App::applySpectralGain(double db) {
    if (m_document.audio().empty()) return;
    if (!m_spectralRegion.active || m_spectralRegion.frames.empty()) {
        setStatus("Drag a time-frequency region first");
        return;
    }
    m_player.stop();
    m_document.setSelection(m_spectralRegion.frames.start,
                            m_spectralRegion.frames.end);
    std::string error;
    if (!m_document.spectralGain(m_spectralRegion.lowHz,
                                 m_spectralRegion.highHz, db, &error)) {
        setStatus(error.empty() ? "Spectral edit failed" : error);
        return;
    }
    markEdited();
    setStatus(db <= -100.0 ? "Spectral region erased" : "Spectral gain applied");
}

void App::applySpectralFilter(bool lowpass) {
    if (m_document.audio().empty()) return;
    if (!m_spectralRegion.active || m_spectralRegion.frames.empty()) {
        setStatus("Drag a time-frequency region first");
        return;
    }
    m_player.stop();
    m_document.setSelection(m_spectralRegion.frames.start,
                            m_spectralRegion.frames.end);
    const double cutoff = lowpass ? m_spectralRegion.highHz
                                  : m_spectralRegion.lowHz;
    std::string error;
    if (!m_document.spectralFilter(lowpass ? "lowpass" : "highpass", cutoff,
                                   &error)) {
        setStatus(error.empty() ? "Spectral filter failed" : error);
        return;
    }
    markEdited();
    setStatus(lowpass ? "Lowpass applied" : "Highpass applied");
}

void App::applySpectralGate() {
    if (m_document.audio().empty()) return;
    if (!m_spectralRegion.active || m_spectralRegion.frames.empty()) {
        setStatus("Drag a time-frequency region first");
        return;
    }
    m_player.stop();
    m_document.setSelection(m_spectralRegion.frames.start,
                            m_spectralRegion.frames.end);
    std::string error;
    if (!m_document.spectralGate(-40.0, &error)) {
        setStatus(error.empty() ? "Spectral gate failed" : error);
        return;
    }
    markEdited();
    setStatus("Spectral gate applied");
}

void App::openDialog(DialogMode mode, const std::string& title,
                     const std::string& initialPath, bool save) {
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
        if (m_dialogMode == DialogMode::None) loadFile(path);
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
        }
        return;
    }
    const bool ctrl = (event.keysym.mod & KMOD_CTRL) != 0;
    const bool shift = (event.keysym.mod & KMOD_SHIFT) != 0;
    if (ctrl && key == SDLK_o) {
        openDialog(DialogMode::Open, "Open audio or project", m_audioPath,
                   false);
    } else if (ctrl && shift && key == SDLK_s) {
        openDialog(DialogMode::SaveProject, "Save project", m_projectPath,
                   true);
    } else if (ctrl && key == SDLK_s) {
        openDialog(DialogMode::SaveAudio, "Export audio", m_audioPath, true);
    } else if (ctrl && key == SDLK_z) {
        m_player.stop();
        if (m_document.undo()) {
            markEdited();
            setStatus("Undone");
        } else {
            setStatus("Nothing to undo");
        }
    } else if (ctrl && (key == SDLK_y ||
                        (shift && key == SDLK_z))) {
        m_player.stop();
        if (m_document.redo()) {
            markEdited();
            setStatus("Redone");
        } else {
            setStatus("Nothing to redo");
        }
    } else if (ctrl && key == SDLK_a) {
        m_document.selectAll();
        setStatus("Selected everything");
    } else if (ctrl && key == SDLK_c) {
        setStatus(m_document.copySelection() ? "Copied selection"
                                             : "Select audio first");
    } else if (ctrl && key == SDLK_x) {
        runEdit([this](std::string* error) {
            return m_document.cutSelection(error);
        }, "Cut selection");
    } else if (ctrl && key == SDLK_v) {
        runEdit([this](std::string* error) {
            return m_document.paste(error);
        }, "Pasted");
    } else if (key == SDLK_SPACE) {
        togglePlay();
    } else if (key == SDLK_ESCAPE) {
        if (m_spectralRegion.active) {
            m_spectralRegion.clear();
            setStatus("Spectral selection cleared");
        } else {
            m_document.clearSelection();
            setStatus("Selection cleared");
        }
    } else if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
        runEdit([this](std::string* error) {
            return m_document.deleteSelection(error);
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
        sliceTransients();
    } else if (key == SDLK_m) {
        addMarkerAtPlayhead();
    } else if (key == SDLK_w) {
        extractWavetable();
    } else if (key == SDLK_e) {
        openDialog(DialogMode::ExportSlices, "Export slices to directory",
                   (std::filesystem::current_path() / "slices").string(),
                   true);
    } else if (key == SDLK_r) {
        runEdit([this](std::string* error) {
            return m_document.reverseSelection(error);
        }, "Reversed");
    } else if (key == SDLK_i) {
        runEdit([this](std::string* error) {
            return m_document.invertSelection(error);
        }, "Polarity inverted");
    } else if (key == SDLK_n) {
        runEdit([this](std::string* error) {
            return m_document.normalize(-1.0, error);
        }, "Normalized to -1 dBFS");
    } else if (key == SDLK_f) {
        runEdit([this, shift](std::string* error) {
            return shift ? m_document.fadeOut(error)
                         : m_document.fadeIn(error);
        }, shift ? "Fade out applied" : "Fade in applied");
    } else if (key == SDLK_PLUS || key == SDLK_EQUALS ||
               key == SDLK_KP_PLUS) {
        m_view.zoomAt(1.25, m_waveArea.w * 0.5, m_waveArea.w,
                      m_document.numFrames());
    } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
        m_view.zoomAt(0.8, m_waveArea.w * 0.5, m_waveArea.w,
                      m_document.numFrames());
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
    if (event.button == SDL_BUTTON_LEFT) {
        for (const auto& button : m_buttons) {
            SDL_Point point{event.x, event.y};
            if (button.enabled && SDL_PointInRect(&point, &button.rect)) {
                button.action();
                return;
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
        if (SDL_PointInRect(&point, &m_overviewArea)) {
            const double fraction =
                static_cast<double>(event.x - m_overviewArea.x) /
                std::max(1, m_overviewArea.w);
            m_view.startFrame =
                fraction * m_document.numFrames() -
                m_view.visibleFrames / 2.0;
            m_view.clamp(m_document.numFrames());
            m_panningOverview = true;
            m_lastMouseX = event.x;
            return;
        }
        if (SDL_PointInRect(&point, &m_waveArea)) {
            m_selectionAnchor =
                m_view.frameAt(event.x - m_waveArea.x, m_waveArea.w,
                               m_document.numFrames());
            m_document.setSelection(m_selectionAnchor, m_selectionAnchor);
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
    if (m_draggingSpectrum) {
        m_draggingSpectrum = false;
        if (m_spectralRegion.frames.length() < 2 ||
            m_spectralRegion.highHz <= m_spectralRegion.lowHz) {
            m_spectralRegion.active = false;
        }
    }
}

void App::handleMouseMotion(const SDL_MouseMotionEvent& event) {
    if (m_draggingSelection) {
        const int64_t frame =
            m_view.frameAt(event.x - m_waveArea.x, m_waveArea.w,
                           m_document.numFrames());
        m_document.setSelection(std::min(m_selectionAnchor, frame),
                                std::max(m_selectionAnchor, frame));
    }
    if (m_panning) {
        const int delta = event.x - m_lastMouseX;
        m_view.pan(-delta, m_waveArea.w, m_document.numFrames());
        m_lastMouseX = event.x;
    }
    if (m_panningOverview) {
        const double fraction =
            static_cast<double>(event.x - m_overviewArea.x) /
            std::max(1, m_overviewArea.w);
        m_view.startFrame = fraction * m_document.numFrames() -
                            m_view.visibleFrames / 2.0;
        m_view.clamp(m_document.numFrames());
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
        if (std::abs(x1 - x0) >= 2 && std::abs(y1 - y0) >= 2) {
            const int64_t frame0 = static_cast<int64_t>(
                static_cast<double>(std::min(x0, x1) - m_spectrumArea.x) /
                std::max(1, m_spectrumArea.w) * m_document.numFrames());
            const int64_t frame1 = static_cast<int64_t>(
                static_cast<double>(std::max(x0, x1) - m_spectrumArea.x) /
                std::max(1, m_spectrumArea.w) * m_document.numFrames());
            const double hz0 = m_spectralView.frequencyAtY(
                std::min(y0, y1) - m_spectrumArea.y, m_spectrumArea.h);
            const double hz1 = m_spectralView.frequencyAtY(
                std::max(y0, y1) - m_spectrumArea.y, m_spectrumArea.h);
            m_spectralRegion.frames = {std::max<int64_t>(0, frame0),
                                       std::min<int64_t>(frame1,
                                                         m_document.numFrames())};
            m_spectralRegion.lowHz = std::min(hz0, hz1);
            m_spectralRegion.highHz = std::max(hz0, hz1);
            m_spectralRegion.active = true;
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
    if (m_tab != vwave::EditorTab::Waveform) return;
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    SDL_Point wheelPoint{mouseX, mouseY};
    if (!SDL_PointInRect(&wheelPoint, &m_contentRect)) return;
    if (event.x != 0) {
        m_view.pan(static_cast<double>(event.x) * 30.0, m_waveArea.w,
                   m_document.numFrames());
        return;
    }
    if (event.y == 0) return;
    const double factor = event.y > 0 ? 1.25 : 0.8;
    m_view.zoomAt(factor, mouseX - m_waveArea.x, m_waveArea.w,
                  m_document.numFrames());
}
