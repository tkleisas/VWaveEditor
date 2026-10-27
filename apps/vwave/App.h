#pragma once

#include "AudioPlayer.h"
#include "AudioRecorder.h"

#include "ui/UiRenderer.h"
#include "controllers/LoopController.h"
#include "services/AnalysisService.h"
#include "services/UiDispatcher.h"
#include "ui/DialogController.h"
#include "ui/Widgets.h"

#include "vwave/JobSystem.h"

#include "vwave/Analysis.h"
#include "vwave/Document.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Mastering.h"
#include "vwave/Settings.h"
#include "vwave/Spectral.h"
#include "vwave/Timeline.h"
#include "vwave/UiModel.h"
#include "vwave/Wavetable.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdint>
#include <memory>
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

    using Button = vwave::ui::Button;
    using SliderRef = vwave::ui::SliderRef;
    using TrackControls = vwave::ui::TrackControls;
    using MenuItem = vwave::ui::MenuItem;
    using Menu = vwave::ui::Menu;

    using DialogMode = vwave::ui::DialogController::Mode;

    enum class WaveDragMode {
        None,
        NewSelection,
        SelectionStart,
        SelectionEnd,
        TrackTrimStart,
        TrackTrimEnd,
        TrackStretch,
        SelectionRepeat,
        ClipMove,
        ClipStretch,
        LoopSelection
    };

    void layout();
    void draw();
    void drawMenuBar();
    void drawMenuDropdown();
    void drawSettingsDialog();
    void drawToolbar();
    void drawSidebar();
    void drawTrackSidebar(int& y);
    void drawWaveform(const SDL_Rect& area);
    void drawSpectrum(const SDL_Rect& area);
    void drawSpectrum3D(const SDL_Rect& area);
    void drawWavetable(const SDL_Rect& area);
    void drawMastering(const SDL_Rect& area);
    void drawImage(const SDL_Rect& area);
    void drawOscilloscope(const SDL_Rect& area);
    void drawFooter();
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
    int laneXForFrame(const SDL_Rect& lane, int64_t frame) const;
    int64_t laneFrameAtX(const SDL_Rect& lane, int x) const;
    SDL_Rect selectionScreenRect() const;
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
    void applyDialog(DialogMode mode, const std::string& path);
    void exportSlices(const std::string& directory);
    void extractWavetable();
    void loadWavetable(const std::string& path);
    void saveWavetable(const std::string& path);
    void renderWavetableNote();
    void analyzeMasteringStems();
    void saveMasteringReport(const std::string& path);
    void renderMasteringStems();
    void regenerateActiveTrack();
    void cancelRenderJobs();
    std::shared_ptr<const vwave::Audio> activeAudioSnapshot();
    void toggleSelectionScope();
    bool seekToTime(double seconds, std::string* error = nullptr);
    void goToStart();
    void goToEnd();
    void setLoopToSelection();
    void clearLoop();
    void toggleLoop();
    void setLoopRegion(int64_t start, int64_t end);
    bool loopPlaybackActive() const;
    void cropActiveTrack();
    void shiftActiveTrack(double seconds);
    void repeatActiveSelection(int count);
    void moveActiveSelectionToPlayhead(bool copy);
    void zeroCrossCut();
    void snapActiveSelection();
    void stretchActiveTrack();
    void splitActiveStereoTrack();
    void combineActiveMono(int direction);
    void cycleStereoPadding();
    void toggleClipTool();
    void makeClips();
    void flattenClips();
    void splitClipAtPlayhead();
    void deleteSelectedClip();
    void repeatSelectedClip(int count);
    void moveSelectedClipToPlayhead();
    void stretchSelectedClip();
    int currentClipIndex() const;
    void openTrackContextMenu(int trackIndex, int x, int y);
    void drawContextMenu();
    void closeContextMenu();
    void loadImage(const std::string& path);
    void renderImageToTrack();
    void cycleImagePhaseMode();
    void cycleImageFrequencyScale();
    void cycleImageTimeMapping();
    void generateScopePath();
    void cycleScopeShape();
    void cycleScopeDirection();
    void cycleScopeOversampling();
    void renderScopeToTrack();
    void loadScopePathFile(const std::string& path);
    void saveScopePathFile(const std::string& path);
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
    void ensurePhaseSpectrogram();
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
    vwave::ui::UiRenderer m_ui;
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
    SDL_Rect m_rulerArea{};
    SDL_Rect m_overviewArea{};
    SDL_Rect m_spectrumArea{};
    SDL_Rect m_wavetableArea{};
    SDL_Rect m_frameStripArea{};
    SDL_Rect m_tracksArea{};
    SDL_Rect m_trackHeaderColumn{};
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
    SliderRef m_activeSlider;
    bool m_sliderDragging = false;
    double m_sidebarScroll = 0.0;
    int m_sidebarContentHeight = 0;

    vwave::Timeline m_timeline;
    vwave::Document m_emptyDocument;
    vwave::app::UiDispatcher m_dispatcher;
    vwave::JobSystem m_jobs;
    vwave::JobHandle m_imageJob;
    vwave::JobHandle m_scopeJob;
    vwave::JobHandle m_wavetableJob;
    vwave::app::AnalysisService m_analysis;
    std::shared_ptr<const vwave::Audio> m_audioSnapshot;
    uint64_t m_imageRequest = 0;
    uint64_t m_scopeRequest = 0;
    uint64_t m_wavetableRequest = 0;
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
    vwave::MasteringAnalysis m_mastering;
    vwave::ImageData m_image;
    vwave::ScopePath m_scopePath;
    SDL_Rect m_scopeArea{};
    std::string m_imagePath;
    SDL_Texture* m_imageTexture = nullptr;
    int m_scopeShape = 2;
    int m_scopeDirection = 0;
    int m_scopeOversampling = 2;
    bool m_scopeBandLimited = true;
    bool m_scopeNormalize = true;
    int m_imageFrequencyScale = 1;
    int m_imageTimeMapping = 0;
    int m_imagePhaseMode = 1;
    int m_imageFftIndex = 3;
    bool m_imageInvert = false;
    bool m_imageUseAlpha = true;
    bool m_imageStereo = false;
    bool m_imageNormalize = true;
    bool m_imageFlipHorizontal = false;
    bool m_imageFlipVertical = false;
    int m_masteringProfile = 0;
    bool m_masteringIncludeMuted = false;
    bool m_masteringMuteOriginals = true;
    std::vector<double> m_frameWeights;
    bool m_freeBlend = false;
    vwave::Spectrogram m_spectrogram;
    vwave::PhaseSpectrogram m_phaseSpectrogram;
    bool m_phaseSpectrogramDirty = true;
    bool m_spectrum3D = false;
    bool m_contextMenuOpen = false;
    int m_contextMenuTrack = -1;
    SDL_Point m_contextMenuPosition{};
    SDL_Rect m_contextMenuRect{};
    std::vector<MenuItem> m_contextMenuItems;
    std::vector<SDL_Rect> m_contextMenuItemRects;
    bool m_shiftingTrack = false;
    int m_shiftingTrackIndex = -1;
    int m_shiftStartMouseX = 0;
    int64_t m_shiftStartFrame = 0;
    WaveDragMode m_waveDrag = WaveDragMode::None;
    int m_waveDragTrack = -1;
    int m_waveDragMouseX = 0;
    int64_t m_waveDragFixedFrame = 0;
    int64_t m_waveDragOriginalStart = 0;
    int64_t m_waveDragOriginalEnd = 0;
    int64_t m_waveDragPreviewFrame = 0;
    int m_repeatDragCount = 0;
    int64_t m_clipDragOriginalFrames = 0;
    int m_selectedClip = -1;
    bool m_clipTool = false;
    vwave::StereoPadding m_stereoPadding = vwave::StereoPadding::Back;
    int m_spectrum3DHoverX = -1;
    int m_spectrum3DHoverY = -1;
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
    vwave::app::LoopController m_loop;
    bool m_spectrumClickCandidate = false;
    int m_spectrumClickX = 0;
    int m_spectrumClickY = 0;
    int64_t m_spectrumClickFrame = 0;
    bool m_clickCandidate = false;
    int64_t m_clickFrame = 0;
    int m_clickMouseX = 0;

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

    vwave::ui::DialogController m_dialog;

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
    vwave::ParameterSlider m_imageDuration;
    vwave::ParameterSlider m_imageMinHz;
    vwave::ParameterSlider m_imageMaxHz;
    vwave::ParameterSlider m_imageGamma;
    vwave::ParameterSlider m_imageContrast;
    vwave::ParameterSlider m_imageThreshold;
    vwave::ParameterSlider m_imageLoops;
    vwave::ParameterSlider m_imageIterations;
    vwave::ParameterSlider m_scopePointCount;
    vwave::ParameterSlider m_scopeSize;
    vwave::ParameterSlider m_scopeA;
    vwave::ParameterSlider m_scopeB;
    vwave::ParameterSlider m_scopePhase;
    vwave::ParameterSlider m_scopeTurns;
    vwave::ParameterSlider m_scopeScanHz;
    vwave::ParameterSlider m_scopeDuration;
    vwave::ParameterSlider m_scopeGain;
    vwave::ParameterSlider m_scopeSmoothing;
    vwave::ParameterSlider m_masteringBands;
    vwave::ParameterSlider m_masteringMaxCut;
    vwave::ParameterSlider m_masteringMaxBoost;
    vwave::ParameterSlider m_masteringSmoothing;
    vwave::ParameterSlider m_masteringCeiling;
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
