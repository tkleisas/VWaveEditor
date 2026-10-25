#pragma once

#include "AudioPlayer.h"
#include "AudioRecorder.h"

#include "vwave/Analysis.h"
#include "vwave/Document.h"
#include "vwave/Settings.h"
#include "vwave/Spectral.h"
#include "vwave/Timeline.h"
#include "vwave/UiModel.h"
#include "vwave/Wavetable.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class App {
public:
    App();
    ~App();

    bool init(const std::string& initialPath);
    int run();

private:
    using Color = SDL_Color;

    struct Button {
        SDL_Rect rect{};
        std::string label;
        std::function<void()> action;
        bool accent = false;
        bool active = false;
        bool enabled = true;
        int area = 0;
    };

    struct SliderRef {
        vwave::ParameterSlider* slider = nullptr;
        SDL_Rect track{};
        std::string label;
        int area = 1;
        std::function<void(double)> onChanged;
    };

    struct TrackControls {
        vwave::ParameterSlider gain;
        vwave::ParameterSlider pan;
    };

    struct MenuItem {
        std::string label;
        std::string shortcut;
        std::function<void()> action;
        bool enabled = true;
        bool separator = false;
    };

    struct Menu {
        std::string title;
        SDL_Rect titleRect{};
        std::vector<MenuItem> items;
    };

    enum class DialogMode {
        None,
        Open,
        AddTrack,
        SaveAudio,
        SaveProject,
        SaveWavetable,
        LoadWavetable,
        ExportSlices
    };

    void layout();
    void draw();
    void drawMenuBar();
    void drawMenuDropdown();
    void drawSettingsDialog();
    void drawToolbar();
    void drawSidebar();
    void drawWaveform(const SDL_Rect& area);
    void drawSpectrum(const SDL_Rect& area);
    void drawWavetable(const SDL_Rect& area);
    void drawFooter();
    void drawDialog();
    void drawButton(const Button& button);
    void drawSection(const std::string& title, int& y);
    void addButton(const std::string& label, int x, int y, int w, int h,
                   std::function<void()> action, bool accent = false,
                   bool active = false, bool enabled = true);
    void addSlider(const std::string& label, const std::string& valueText,
                   int x, int y, int w, vwave::ParameterSlider& slider,
                   std::function<void(double)> onChanged = {});
    void setSliderFromX(SliderRef& slider, int x);
    vwave::StretchAlgorithm selectedStretchAlgorithm() const;
    vwave::Range spectralTimeRange() const;
    void sliceCurrent();
    void stretchSelection(bool toNewTrack);
    void pitchSelection(bool toNewTrack);
    void copySelectionToNewTrack();
    void zoomTime(double factor);
    void fitTime();
    void zoomFrequency(double factor);
    void fitFrequency();
    int drawText(const std::string& text, int x, int y, const Color& color,
                 TTF_Font* font);
    void drawTextCentered(const std::string& text, const SDL_Rect& rect,
                          const Color& color, TTF_Font* font);
    int textWidth(const std::string& text, TTF_Font* font) const;
    TTF_Font* fontSmall() const { return m_fontSmall; }
    TTF_Font* fontNormal() const { return m_font; }
    TTF_Font* fontBold() const { return m_fontBold; }
    TTF_Font* fontTitle() const { return m_fontTitle; }

    void handleEvent(const SDL_Event& event);
    void handleKey(const SDL_KeyboardEvent& event);
    void handleMouseDown(const SDL_MouseButtonEvent& event);
    void handleMouseUp(const SDL_MouseButtonEvent& event);
    void handleMouseMotion(const SDL_MouseMotionEvent& event);
    void handleWheel(const SDL_MouseWheelEvent& event);

    bool loadFile(const std::string& path);
    bool loadAudio(const std::string& path);
    bool addTrackFile(const std::string& path);
    bool loadProject(const std::string& path);
    void saveAudio(const std::string& path);
    void saveProject(const std::string& path);
    void setTab(vwave::EditorTab tab);
    void togglePlay();
    void stopPlayback();
    void updatePlayback();
    void runEdit(const std::function<bool(std::string*)>& edit,
                 const std::string& success);
    void markEdited();
    void clearAnalysis();
    void analyzeBeats();
    void detectPitch();
    void openDialog(DialogMode mode, const std::string& title,
                    const std::string& initialPath, bool save);
    void confirmDialog();
    void cancelDialog();
    void exportSlices(const std::string& directory);
    void extractWavetable();
    void loadWavetable(const std::string& path);
    void saveWavetable(const std::string& path);
    void renderWavetableNote();
    void syncWavetableDesignControls();
    void addWavetableFrame(bool duplicate);
    void addHarmonicWavetableFrame();
    void deleteWavetableFrame();
    void cycleWavetableMorphMode();
    void cycleWavetableInterpolation();
    void cycleWavetableOversampling();
    void applySpectralGain(double db);
    void applySpectralFilter(bool lowpass);
    void applySpectralGate();
    void applySpectralBlur();
    void addMarkerAtPlayhead();
    void ensureSpectrogram();
    void ensureOverview();
    void setStatus(const std::string& text, Uint32 milliseconds = 3500);
    void updateWindowTitle();
    vwave::Document& activeDocument();
    const vwave::Document& activeDocument() const;
    bool hasTracks() const { return m_timeline.trackCount() > 0; }
    void selectTrack(int index);
    void removeTrack(int index);
    void addEmptyTrack();
    void toggleRecording();
    void startRecording();
    void stopRecording();
    void updateRecording();
    void loadSettings();
    void saveSettings();
    void openSettings();
    void closeSettings();
    void refreshAudioDevices();
    void cycleOutputDevice(int direction);
    void cycleInputDevice(int direction);
    void setTheme(std::string theme);
    void syncTrackControls();
    void syncSelectionFromActive();
    void updateMixer();
    void drawTrackHeader(const SDL_Rect& header, int index);

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_fontSmall = nullptr;
    TTF_Font* m_font = nullptr;
    TTF_Font* m_fontBold = nullptr;
    TTF_Font* m_fontTitle = nullptr;

    SDL_Rect m_menubarRect{};
    SDL_Rect m_toolbarRect{};
    SDL_Rect m_sidebarRect{};
    SDL_Rect m_contentRect{};
    SDL_Rect m_footerRect{};
    SDL_Rect m_waveArea{};
    SDL_Rect m_overviewArea{};
    SDL_Rect m_spectrumArea{};
    SDL_Rect m_wavetableArea{};
    SDL_Rect m_frameStripArea{};
    SDL_Rect m_tracksArea{};
    SDL_Rect m_trackHeaderColumn{};
    std::vector<Button> m_buttons;
    std::vector<SliderRef> m_sliders;
    std::vector<Menu> m_menus;
    int m_openMenu = -1;
    SDL_Rect m_openMenuRect{};
    std::vector<SDL_Rect> m_menuItemRects;
    vwave::AppSettings m_settings;
    std::string m_settingsPath;
    bool m_settingsOpen = false;
    std::vector<std::string> m_inputDevices;
    std::vector<std::string> m_outputDevices;
    int m_inputDeviceIndex = 0;
    int m_outputDeviceIndex = 0;
    SDL_Rect m_settingsOutputPrev{};
    SDL_Rect m_settingsOutputNext{};
    SDL_Rect m_settingsInputPrev{};
    SDL_Rect m_settingsInputNext{};
    SDL_Rect m_settingsThemePrev{};
    SDL_Rect m_settingsThemeNext{};
    SDL_Rect m_settingsCloseRect{};
    std::vector<TrackControls> m_trackControls;
    std::vector<SDL_Rect> m_trackHeaderRects;
    std::vector<SDL_Rect> m_trackLaneRects;
    int m_controlArea = 0;
    SliderRef m_activeSlider;
    bool m_sliderDragging = false;
    double m_sidebarScroll = 0.0;
    int m_sidebarContentHeight = 0;

    vwave::Timeline m_timeline;
    vwave::Document m_emptyDocument;
    AudioPlayer m_player;
    AudioRecorder m_recorder;
    bool m_recording = false;
    int64_t m_recordStartFrame = 0;
    int m_recordTarget = -1;
    std::vector<float> m_recordedInterleaved;
    int m_recordedChannels = 0;
    int m_recordedSampleRate = 0;
    vwave::EditorTab m_tab = vwave::EditorTab::Waveform;
    vwave::WaveformViewport m_view;
    vwave::SpectralViewport m_spectralView;
    vwave::SpectralRegion m_spectralRegion;
    vwave::BeatAnalysis m_beats;
    vwave::PitchEstimate m_pitch;
    bool m_hasBeats = false;
    bool m_hasPitch = false;
    vwave::Wavetable m_wavetable;
    vwave::WavetableViewport m_wavetableView;
    std::vector<double> m_frameWeights;
    bool m_freeBlend = false;
    vwave::Spectrogram m_spectrogram;
    SDL_Texture* m_spectrogramTexture = nullptr;
    int m_spectrogramTextureWidth = 0;
    int m_spectrogramTextureHeight = 0;
    bool m_spectrogramDirty = true;
    std::vector<vwave::PeakBin> m_overviewPeaks;
    bool m_overviewDirty = true;

    std::string m_audioPath;
    std::string m_projectPath;
    std::string m_status;
    Uint32 m_statusUntil = 0;
    bool m_running = true;
    int64_t m_playhead = 0;

    bool m_draggingSelection = false;
    int64_t m_selectionAnchor = 0;
    double m_trackScroll = 0.0;
    int m_trackContentHeight = 0;
    bool m_panning = false;
    bool m_panningOverview = false;
    int m_lastMouseX = 0;

    bool m_draggingSpectrum = false;
    int m_spectrumStartX = 0;
    int m_spectrumStartY = 0;

    bool m_drawingWavetable = false;
    int m_lastWavetableSample = -1;

    DialogMode m_dialogMode = DialogMode::None;
    std::string m_dialogPath;
    std::string m_dialogTitle;
    bool m_dialogSave = false;
    SDL_Rect m_dialogConfirmRect{};
    SDL_Rect m_dialogCancelRect{};

    vwave::ParameterSlider m_gainDb;
    vwave::ParameterSlider m_normalizePeak;
    vwave::ParameterSlider m_stretchRatio;
    vwave::ParameterSlider m_pitchSemitones;
    vwave::ParameterSlider m_beatSensitivity;
    vwave::ParameterSlider m_sliceSensitivity;
    vwave::ParameterSlider m_minSliceMs;
    vwave::ParameterSlider m_beatsPerSlice;
    vwave::ParameterSlider m_spectralLow;
    vwave::ParameterSlider m_spectralHigh;
    vwave::ParameterSlider m_spectralGain;
    vwave::ParameterSlider m_spectralGate;
    vwave::ParameterSlider m_spectralCutoff;
    vwave::ParameterSlider m_spectralBlurTime;
    vwave::ParameterSlider m_spectralBlurFrequency;
    vwave::ParameterSlider m_wavetableFrameCount;
    vwave::ParameterSlider m_synthNote;
    vwave::ParameterSlider m_synthRoot;
    vwave::ParameterSlider m_synthDuration;
    vwave::ParameterSlider m_synthMorph;
    vwave::ParameterSlider m_synthUnison;
    vwave::ParameterSlider m_synthGain;
    vwave::ParameterSlider m_framePositionControl;
    vwave::ParameterSlider m_frameWeightControl;
    vwave::ParameterSlider m_tableRootControl;
    vwave::ParameterSlider m_harmonicCount;
    vwave::ParameterSlider m_harmonicLimit;
    vwave::ParameterSlider m_quantizeBits;
    int m_stretchAlgorithm = 0;
    int m_sliceMode = 0;
    int m_processTarget = 0;
    bool m_filterLowpass = true;
    int m_frameSizeIndex = 3;
};
