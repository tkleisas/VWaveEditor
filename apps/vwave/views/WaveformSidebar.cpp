#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace vwave::ui;

void App::drawTrackSidebar(int& y) {
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
    addButton(std::string("Loop: ") + (m_loop.enabled() ? "On" : "Off"), 18, y,
              buttonWidth, buttonHeight, [this] { toggleLoop(); }, false,
              m_loop.enabled(), !m_timeline.empty());
    addButton("Loop Selection", 18 + buttonWidth + gap, y, buttonWidth,
              buttonHeight, [this] { setLoopToSelection(); }, false, false,
              hasSelection);
    y += buttonHeight + 6;
    addButton("Clear Loop", 18, y, buttonWidth, buttonHeight,
              [this] { clearLoop(); }, false, false,
              m_loop.end() > m_loop.start());
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

}
