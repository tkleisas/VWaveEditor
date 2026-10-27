#include "controllers/SelectionController.h"

#include "ui/UiFormat.h"
#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vwave::app {

SelectionController::SelectionController(
    vwave::Timeline& timeline, AudioPlayer& player, int64_t& playhead,
    vwave::ParameterSlider& stretchRatio, EditRunner runEdit,
    ChangeSink changed, StatusSink status)
    : m_timeline(timeline),
      m_player(player),
      m_playhead(playhead),
      m_stretchRatio(stretchRatio),
      m_runEdit(std::move(runEdit)),
      m_changed(std::move(changed)),
      m_status(std::move(status)) {}

vwave::Document* SelectionController::activeDocument() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return nullptr;
    return &m_timeline.track(index).document;
}

void SelectionController::toggleScope() {
    m_timeline.setSelectionScope(
        m_timeline.selectionScope() == vwave::SelectionScope::AllTracks
            ? vwave::SelectionScope::ActiveTrack
            : vwave::SelectionScope::AllTracks);
    m_status(m_timeline.selectionScope() ==
                     vwave::SelectionScope::ActiveTrack
                 ? "Selection: active track"
                 : "Selection: all tracks");
}

void SelectionController::cropActiveTrack() {
    if (m_timeline.empty()) return;
    const int index = m_timeline.activeTrack();
    if (!m_timeline.selection().active) {
        m_status("Select a range first");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.cropTrack(index, m_timeline.selectedRange(), &error)) {
        m_status(error.empty() ? "Crop failed" : error);
        return;
    }
    m_changed(true);
    m_status("Cropped track " + m_timeline.track(index).name);
}

void SelectionController::shiftActiveTrack(double seconds) {
    if (m_timeline.empty() || m_timeline.sampleRate() <= 0) return;
    const int64_t delta = static_cast<int64_t>(
        std::llround(seconds * static_cast<double>(m_timeline.sampleRate())));
    std::string error;
    if (!m_timeline.shiftTrack(m_timeline.activeTrack(), delta, &error)) {
        m_status(error.empty() ? "Shift failed" : error);
        return;
    }
    m_changed(false);
    m_status("Shifted track by " + ui::formatSeconds(seconds));
}

void SelectionController::repeatSelection(int count) {
    auto* document = activeDocument();
    if (!document || !document->selection().active) {
        m_status("Select a range first");
        return;
    }
    count = std::clamp(count, 1, 64);
    m_runEdit([document, count](std::string* error) {
        return document->repeatSelection(count, error);
    }, "Repeated selection " + std::to_string(count) + "x");
}

void SelectionController::moveSelectionToPlayhead(bool copy) {
    auto* document = activeDocument();
    if (!document || !document->selection().active) {
        m_status("Select a range first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t destination = std::clamp<int64_t>(
        m_playhead - track.startFrame, 0, document->numFrames());
    m_runEdit([document, destination, copy](std::string* error) {
        return document->moveSelectionTo(destination, copy, error);
    }, copy ? "Copied selection to playhead"
            : "Moved selection to playhead");
}

void SelectionController::zeroCrossCut() {
    auto* document = activeDocument();
    if (!document || !document->selection().active) {
        m_status("Select a range first");
        return;
    }
    const int64_t radius =
        std::max<int64_t>(1, document->sampleRate() / 100);
    const vwave::Range snapped = vwave::snapRangeToZeroCrossings(
        document->audio(), document->selectedRange(), radius);
    if (snapped.length() <= 0) {
        m_status("Cannot snap selection");
        return;
    }
    document->setSelection(snapped.start, snapped.end);
    m_runEdit([document](std::string* error) {
        return document->deleteSelection(error);
    }, "Zero-cross cut");
}

void SelectionController::snapSelection() {
    auto* document = activeDocument();
    if (!document || !document->selection().active) {
        m_status("Select a range first");
        return;
    }
    const int64_t radius =
        std::max<int64_t>(1, document->sampleRate() / 100);
    const vwave::Range snapped = vwave::snapRangeToZeroCrossings(
        document->audio(), document->selectedRange(), radius);
    document->setSelection(snapped.start, snapped.end);
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    m_timeline.setSelection(snapped.start + track.startFrame,
                            snapped.end + track.startFrame);
    m_status("Selection snapped to zero crossings");
}

void SelectionController::stretchTrack(vwave::StretchAlgorithm algorithm) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) {
        m_status("Open an audio file first");
        return;
    }
    m_runEdit([document, this, algorithm](std::string* error) {
        return document->timeStretchTrack(
            m_stretchRatio.value, algorithm, error);
    }, "Track stretched");
}

void SelectionController::addMarkerAtPlayhead() {
    auto* document = activeDocument();
    if (!document) return;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t frame = m_playhead - track.startFrame;
    if (frame < 0 || frame > document->numFrames()) {
        m_status("Playhead is outside the active track");
        return;
    }
    if (document->addMarker(frame, "Marker")) {
        m_status("Marker added at " +
                 ui::formatSeconds(static_cast<double>(frame) /
                                   std::max(1, document->sampleRate())));
    }
}

} // namespace vwave::app
