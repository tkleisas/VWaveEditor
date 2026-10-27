#include "controllers/SpectralController.h"

#include <algorithm>
#include <utility>

namespace vwave::app {

SpectralController::SpectralController(vwave::Timeline& timeline,
                                       AudioPlayer& player,
                                       EditRunner runEdit,
                                       StatusSink status)
    : m_timeline(timeline),
      m_player(player),
      m_runEdit(std::move(runEdit)),
      m_status(std::move(status)) {}

vwave::Document* SpectralController::activeDocument() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return nullptr;
    return &m_timeline.track(index).document;
}

void SpectralController::clear() { m_region.clear(); }

vwave::Range SpectralController::timeRange() const {
    if (m_region.active && !m_region.frames.empty()) return m_region.frames;
    const int index = m_timeline.activeTrack();
    if (index < 0) return {};
    const auto& document = m_timeline.track(index).document;
    if (document.selection().active) return document.selectedRange();
    return {0, document.numFrames()};
}

void SpectralController::applyGain(double db) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    const double lowHz = m_region.lowHz;
    const double highHz = m_region.highHz;
    m_runEdit([document, db, range, lowHz, highHz](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralGain(lowHz, highHz, db, error);
    }, "Spectral gain applied");
}

void SpectralController::applyFilter(bool lowpass, double cutoffHz) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    m_runEdit([document, lowpass, cutoffHz, range](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralFilter(lowpass ? "lowpass" : "highpass",
                                        cutoffHz, error);
    }, lowpass ? "Spectral lowpass applied" : "Spectral highpass applied");
}

void SpectralController::applyGate(double thresholdDb) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    m_runEdit([document, thresholdDb, range](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralGate(thresholdDb, error);
    }, "Spectral gate applied");
}

void SpectralController::applyBlur(int timeRadius, int frequencyRadius) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    m_runEdit([document, timeRadius, frequencyRadius, range](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralBlur(timeRadius, frequencyRadius, error);
    }, "Spectral blur applied");
}

} // namespace vwave::app
