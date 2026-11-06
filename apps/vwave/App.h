#pragma once

#include "AudioPlayer.h"
#include "AudioRecorder.h"

#include "ui/UiRenderer.h"
#include "controllers/AnalysisController.h"
#include "controllers/ClipController.h"
#include "controllers/LoopController.h"
#include "controllers/PlaybackController.h"
#include "controllers/RecordingController.h"
#include "controllers/SelectionController.h"
#include "controllers/SettingsController.h"
#include "controllers/ImageController.h"
#include "controllers/MasteringController.h"
#include "controllers/ScopeController.h"
#include "controllers/WavetableController.h"
#include "controllers/SpectralController.h"
#include "views/WaveformPeakCache.h"
#include "controllers/StereoController.h"
#include "controllers/TrackController.h"
#include "services/AnalysisService.h"
#include "services/BlockPrefetchService.h"
#include "services/FileLoadController.h"
#include "services/FileSaveController.h"
#include "services/UiDispatcher.h"
#include "ui/DialogController.h"
#include "ui/editor/ControlPanel.h"
#include "ui/editor/MenuPopup.h"
#include "ui/editor/TimelineCanvas.h"
#include "ui/editor/SidebarSlider.h"
#include "ui/framework/PushButton.h"
#include "ui/framework/WidgetHost.h"
#include "ui/sdl/SdlCursorController.h"
#include "ui/sdl/SdlEventAdapter.h"
#include "ui/sdl/SdlPainter.h"
#include "ui/Widgets.h"

#include "vwave/JobSystem.h"

#include "vwave/Analysis.h"
#include "vwave/Document.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Mastering.h"
#include "vwave/Music.h"
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
        SelectionStretch,
        SelectionMove,
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
    void createToolbarWidgets();
    void syncToolbarWidgets();
    bool handleToolbarInput(const SDL_Event& event);
    void addSidebarButton(const std::string& label, int x, int y, int w,
                          int h, std::function<void()> action, bool accent,
                          bool active, bool enabled);
    void addSidebarSlider(const std::string& label,
                          const std::string& valueText, int x, int y, int w,
                          vwave::ParameterSlider& slider,
                          std::function<void(double)> onChanged);
    bool handleSidebarInput(const SDL_Event& event);
    bool handleTrackHeaderInput(const SDL_Event& event);
    bool handleSettingsInput(const SDL_Event& event);
    bool handleMenuInput(const SDL_Event& event);
    bool handleContextMenuInput(const SDL_Event& event);
    bool handleDialogInput(const SDL_Event& event);
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
    void drawSection(const std::string& title, int& y);
    void addButton(const std::string& label, int x, int y, int w, int h,
                   std::function<void()> action, bool accent = false,
                   bool active = false, bool enabled = true);
    void addSlider(const std::string& label, const std::string& valueText,
                   int x, int y, int w, vwave::ParameterSlider& slider,
                   std::function<void(double)> onChanged = {});
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
    void handleWaveformMouseDown(const SDL_MouseButtonEvent& event);
    void handleSpectrumMouseDown(const SDL_MouseButtonEvent& event);
    void handleWavetableMouseDown(const SDL_MouseButtonEvent& event);
    void handleOscilloscopeMouseDown(const SDL_MouseButtonEvent& event);
    void handleOscilloscopeMouseMotion(const SDL_MouseMotionEvent& event);
    void handleOscilloscopeMouseUp(const SDL_MouseButtonEvent& event);
    void handleWaveformMouseMotion(const SDL_MouseMotionEvent& event);
    void handleSpectrumHover(const SDL_MouseMotionEvent& event);
    void handleSpectrumMouseMotion(const SDL_MouseMotionEvent& event);
    void handleWavetableMouseMotion(const SDL_MouseMotionEvent& event);
    void handleWaveformMouseUp(const SDL_MouseButtonEvent& event);
    void updateWaveDragPreview(int mouseX, int mouseY);
    void updateWaveDragAutoscroll();
    void handleSpectrumMouseUp(const SDL_MouseButtonEvent& event);
    vwave::ui::CursorShape waveformCursorAt(int x, int y) const;
    void refreshTimelineCanvas() const;
    void updateUiCursor(int x, int y);
    void handleWheel(const SDL_MouseWheelEvent& event);

    bool loadFile(const std::string& path);
    bool loadAudio(const std::string& path);
    bool addTrackFile(const std::string& path);
    bool loadProject(const std::string& path);
    void applyLoadedProject(vwave::Timeline timeline,
                            const std::string& projectPath);
    void saveAudio(const std::string& path);
    void saveProject(const std::string& path);
    void updateAutosave();
    void recoverAutosave();
    void removeAutosaveFiles(const std::string& projectPath);
    void requestPrefetch();
    void setTab(vwave::EditorTab tab);
    void togglePlay();
    void stopPlayback();
    void updatePlayback();
    void runEdit(const std::function<bool(std::string*)>& edit,
                 const std::string& success);
    void markEdited();
    void markDirty();
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
    vwave::app::ScopeShapeParams scopeShapeParams() const;
    vwave::app::ScopeRenderParams scopeRenderParams() const;
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
    void toggleScopeXYMode();
    void importScopeImage(const std::string& path);
    void importScopeSvg(const std::string& path);
    void syncScopeKeyframeControls();
    void toggleScopePreview();
    void analyzeScopeFrameTone(bool usePreview = false);
    void analyzeScopeAudioTone(const vwave::Audio& audio, double timeSeconds,
                               int windowSamples);
    void queueScopeToneRefresh();
    void updateScopeToneRefresh();
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
    void syncTrackControls();
    void syncSelectionFromActive();
    void updateMixer();
    void drawTrackHeader(const SDL_Rect& header, int index);

    enum class ToolbarButton {
        Waveform,
        Spectrum,
        Wavetable,
        Mastering,
        Image,
        Scope,
        Open,
        AddTrack,
        SaveWav,
        Project,
        Play,
        Stop,
        ZoomOut,
        ZoomIn,
        Fit,
        Start,
        End,
        Time,
        Count
    };

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    vwave::ui::UiRenderer m_ui;
    std::unique_ptr<vwave::ui::SdlPainter> m_uiPainter;
    vwave::ui::WidgetHost m_toolbarHost;
    std::unique_ptr<vwave::ui::Widget> m_toolbarRoot;
    std::vector<vwave::ui::PushButton*> m_toolbarButtons;
    vwave::ui::WidgetHost m_sidebarHost;
    std::unique_ptr<vwave::ui::Widget> m_sidebarRoot;
    std::vector<vwave::ui::PushButton*> m_sidebarButtons;
    std::vector<vwave::ui::SidebarSlider*> m_sidebarSliders;
    size_t m_sidebarButtonIndex = 0;
    size_t m_sidebarSliderIndex = 0;
    bool m_buildingSidebar = false;
    vwave::ui::ControlPanel m_trackHeaderControls;
    bool m_buildingTrackHeader = false;
    mutable vwave::ui::TimelineCanvas m_timelineCanvas;
    mutable vwave::ui::TimelineCanvasLayout m_timelineCanvasLayout;
    vwave::ui::ControlPanel m_settingsControls;
    vwave::ui::ControlPanel m_menuBarControls;
    vwave::ui::MenuPopup m_menuPopup;
    vwave::ui::WidgetHost m_menuHost;
    vwave::ui::SdlEventAdapter m_uiEventAdapter;
    vwave::ui::SdlCursorController m_cursorController;
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
    double m_sidebarScroll = 0.0;
    int m_sidebarContentHeight = 0;

    vwave::Timeline m_timeline;
    vwave::Document m_emptyDocument;
    vwave::app::UiDispatcher m_dispatcher;
    vwave::JobSystem m_jobs;
    vwave::app::AnalysisService m_analysis;
    vwave::app::FileLoadController m_fileLoader;
    vwave::app::FileSaveController m_fileSaver;
    vwave::app::BlockPrefetchService m_prefetch;
    vwave::app::AnalysisController m_analysisData;
    std::shared_ptr<const vwave::Audio> m_audioSnapshot;
    AudioPlayer m_player;
    vwave::app::StereoController m_stereo;
    vwave::EditorTab m_tab = vwave::EditorTab::Waveform;
    vwave::WaveformViewport m_view;
    vwave::SpectralViewport m_spectralView;
    SDL_Rect m_scopeArea{};
    SDL_Rect m_scopeTimelineArea{};
    bool m_scopeLoopDragging = false;
    int64_t m_scopeLoopAnchorFrame = 0;
    double m_scopeLoopAnchorTime = 0.0;
    double m_scopeLoopStartTime = 0.0;
    double m_scopeLoopEndTime = 0.0;
    bool m_scopeLoopActive = false;
    std::shared_ptr<vwave::Audio> m_scopePreviewAudio;
    int64_t m_scopePreviewTime = 0;
    bool m_scopePreviewPlaying = false;
    uint64_t m_scopePreviewRevision = 0;
    vwave::PitchEstimate m_scopeFrameTone;
    vwave::NoteInfo m_scopeFrameNote;
    bool m_scopeFrameToneValid = false;
    double m_scopeFrameToneTime = 0.0;
    double m_scopeFrameToneWindowMs = 0.0;
    bool m_scopeFrameToneFromPreview = false;
    Uint32 m_scopeNextToneAnalysisTicks = 0;
    bool m_scopeToneRefreshPending = false;
    Uint32 m_scopeToneRefreshAfter = 0;
    vwave::Spectrogram m_spectrogram;
    vwave::PhaseSpectrogram m_phaseSpectrogram;
    bool m_phaseSpectrogramDirty = true;
    bool m_spectrum3D = false;
    bool m_contextMenuOpen = false;
    int m_contextMenuTrack = -1;
    SDL_Point m_contextMenuPosition{};
    SDL_Rect m_contextMenuRect{};
    std::vector<vwave::ui::PopupItem> m_contextMenuItems;
    vwave::ui::MenuPopup m_contextMenuPopup;
    vwave::ui::WidgetHost m_contextMenuHost;
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
    int m_spectrum3DHoverX = -1;
    int m_spectrum3DHoverY = -1;
    SDL_Texture* m_spectrogramTexture = nullptr;
    int m_spectrogramTextureWidth = 0;
    int m_spectrogramTextureHeight = 0;
    bool m_spectrogramDirty = true;
    bool m_scopeXYMode = false;
    bool m_scopeXYDirty = true;
    SDL_Texture* m_scopeXYTexture = nullptr;
    int m_scopeXYTextureSize = 0;
    int m_scopeXYTrack = -1;
    uint64_t m_scopeXYRevision = 0;
    int64_t m_scopeXYWindowStart = -1;
    int64_t m_scopeXYWindowFrames = 0;
    std::vector<vwave::PeakBin> m_overviewPeaks;
    bool m_overviewDirty = true;

    std::string m_audioPath;
    std::string m_projectPath;
    bool m_modified = false;
    bool m_autosavePending = false;
    bool m_autosaveInProgress = false;
    uint64_t m_editGeneration = 0;
    uint64_t m_prefetchGeneration = 0;
    Uint32 m_lastEditTicks = 0;
    Uint32 m_autosaveRetryAfter = 0;
    std::string m_status;
    Uint32 m_statusUntil = 0;
    bool m_running = true;
    int64_t m_playhead = 0;
    vwave::app::TrackController m_tracks;
    vwave::app::LoopController m_loop;
    vwave::app::PlaybackController m_playback;
    vwave::app::RecordingController m_recordingController;
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
    vwave::app::ClipController m_clips;
    vwave::app::SelectionController m_selection;
    vwave::app::SpectralController m_spectralController;
    vwave::app::MasteringController m_masteringController;
    vwave::app::ImageController m_imageController;
    vwave::app::ScopeController m_scopeController;
    vwave::app::WavetableController m_wavetableController;
    vwave::app::SettingsController m_settingsController;
    vwave::app::WaveformPeakCache m_peakCache;
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
    vwave::ParameterSlider m_scopeKeyTime;
    vwave::ParameterSlider m_scopeTranslateX;
    vwave::ParameterSlider m_scopeTranslateY;
    vwave::ParameterSlider m_scopeRotation;
    vwave::ParameterSlider m_scopeScale;
    vwave::ParameterSlider m_scopeXYWindowMs;
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
