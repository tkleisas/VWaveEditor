#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace vwave::app;
using namespace vwave::ui;

void App::drawSidebar() {
    m_ui.setControlArea(1);
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
    if (m_analysisData.hasBeats() || m_analysisData.hasPitch()) {
        std::ostringstream analysis;
        if (m_analysisData.hasBeats()) {
            analysis << std::fixed << std::setprecision(1) << m_analysisData.beats().bpm
                     << " BPM";
        }
        if (m_analysisData.hasPitch()) {
            if (m_analysisData.hasBeats()) analysis << "  ";
            analysis << std::fixed << std::setprecision(1) << m_analysisData.pitch().frequency
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
    if (m_tab == vwave::EditorTab::Spectrum) {
        drawSection("SPECTRAL", y);
        std::string regionText = "Region: whole document";
        if (m_spectralController.region().active && !m_spectralController.region().frames.empty()) {
            std::ostringstream out;
            out << "Region: "
                << formatSeconds(static_cast<double>(
                                     m_spectralController.region().frames.start) /
                                 std::max(1, activeDocument().sampleRate()))
                << " - "
                << formatSeconds(static_cast<double>(
                                     m_spectralController.region().frames.end) /
                                 std::max(1, activeDocument().sampleRate()))
                << "  " << formatHz(m_spectralController.region().lowHz) << " - "
                << formatHz(m_spectralController.region().highHz);
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
                      kMasteringProfileNames[m_masteringController.profile()],
                  18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] { m_masteringController.cycleProfile(); });
        y += buttonHeight + 6;
        addButton("Analyze Stems", 18, y, buttonWidth * 2 + gap, 26,
                  [this] { analyzeMasteringStems(); }, true, false,
                  !m_timeline.empty());
        y += 32;
        addSlider("Bands", formatInt(m_masteringBands.value), 18, y, 256,
                  m_masteringBands, [this](double) { m_masteringController.clear(); });
        y += 30;
        addSlider("Max cut", formatDb(-m_masteringMaxCut.value), 18, y, 256,
                  m_masteringMaxCut, [this](double) { m_masteringController.clear(); });
        y += 30;
        addSlider("Max boost", formatDb(m_masteringMaxBoost.value), 18, y,
                  256, m_masteringMaxBoost,
                  [this](double) { m_masteringController.clear(); });
        y += 30;
        addSlider("Smoothing", formatInt(m_masteringSmoothing.value), 18, y,
                  256, m_masteringSmoothing,
                  [this](double) { m_masteringController.clear(); });
        y += 30;
        addButton(std::string("Include Muted: ") +
                      (m_masteringController.includeMuted() ? "On" : "Off"),
                  18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] { m_masteringController.toggleIncludeMuted(); });
        y += buttonHeight + 6;
        addSlider("Ceiling", formatDb(m_masteringCeiling.value), 18, y, 256,
                  m_masteringCeiling, [this](double) { m_masteringController.clear(); });
        y += 30;
        addButton(std::string("Mute originals: ") +
                      (m_masteringController.muteOriginals() ? "On" : "Off"),
                  18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] { m_masteringController.toggleMuteOriginals(); });
        y += buttonHeight + 6;
        addButton("Render Processed Stems", 18, y,
                  buttonWidth * 2 + gap, 26,
                  [this] { renderMasteringStems(); }, true, false,
                  m_masteringController.valid());
        y += 32;
        addButton("Export Report", 18, y, buttonWidth * 2 + gap, buttonHeight,
                  [this] {
                      openDialog(DialogMode::SaveMasteringReport,
                                 "Export mastering report",
                                 (std::filesystem::current_path() /
                                  "mastering.json").string(),
                                 true);
                  }, false, false, m_masteringController.valid());
        y += buttonHeight + 6;
        if (m_masteringController.valid()) {
            std::ostringstream summary;
            summary << m_masteringController.analysis().stems.size() << " stems  "
                    << m_masteringController.analysis().bandCount << " bands";
            drawText(summary.str(), 18, y, kTextDim, fontSmall());
            y += 18;
            std::ostringstream mix;
            mix << "Mix RMS " << formatDb(m_masteringController.analysis().mixRmsDb)
                << "  TP " << formatDb(m_masteringController.analysis().mixTruePeakDb);
            drawText(mix.str(), 18, y, kTextDim, fontSmall());
            y += 18;
            std::ostringstream phase;
            phase << "Phase " << std::fixed << std::setprecision(2)
                  << m_masteringController.analysis().phaseCoherence;
            drawText(phase.str(), 18, y, kTextFaint, fontSmall());
            y += 18;
        }
    } else if (m_tab == vwave::EditorTab::Image) {
        const bool hasImage = m_imageController.valid();
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
            info << m_imageController.image().width << " x "
                 << m_imageController.image().height << "   "
                 << truncateText(baseName(m_imageController.path()),
                                 fontSmall(), 180);
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
                        kImageFrequencyNames[m_imageController.frequencyScale()],
                    [this] {
                        cycleImageFrequencyScale();
                    }},
                   {std::string("Time: ") +
                        kImageTimeNames[m_imageController.timeMapping()],
                    [this] { cycleImageTimeMapping(); }}});
        imageGrid({{std::string("Phase: ") +
                        kImagePhaseNames[m_imageController.phaseMode()],
                    [this] { cycleImagePhaseMode(); }},
                   {"FFT: " + std::to_string(kFrameSizes[m_imageController.fftIndex()]),
                    [this] { m_imageController.cycleFftIndex(); }}});
        imageGrid({{std::string("Channels: ") +
                        (m_imageController.stereo() ? "Stereo" : "Mono"),
                    [this] { m_imageController.toggleStereo(); }},
                   {std::string("Invert: ") +
                        (m_imageController.invert() ? "On" : "Off"),
                    [this] { m_imageController.toggleInvert(); }}});
        imageGrid({{std::string("Alpha: ") +
                        (m_imageController.useAlpha() ? "On" : "Off"),
                    [this] { m_imageController.toggleUseAlpha(); }},
                   {std::string("Normalize: ") +
                        (m_imageController.normalize() ? "On" : "Off"),
                    [this] { m_imageController.toggleNormalize(); }}});
        imageGrid({{std::string("Flip H: ") +
                        (m_imageController.flipHorizontal() ? "On" : "Off"),
                    [this] { m_imageController.toggleFlipHorizontal(); }},
                   {std::string("Flip V: ") +
                        (m_imageController.flipVertical() ? "On" : "Off"),
                    [this] { m_imageController.toggleFlipVertical(); }}});
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

    drawTrackSidebar(y);

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

