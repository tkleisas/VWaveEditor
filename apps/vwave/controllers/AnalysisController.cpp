#include "controllers/AnalysisController.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace vwave::app {

void AnalysisController::clear() {
    m_beats = {};
    m_pitch = {};
    m_hasBeats = false;
    m_hasPitch = false;
}

void AnalysisController::analyzeBeats(
    std::shared_ptr<const vwave::Audio> audio, double sensitivity) {
    if (!audio || audio->empty()) {
        m_status("Open an audio file first");
        return;
    }
    m_status("Analyzing beats...");
    m_service.requestBeats(
        std::move(audio), sensitivity,
        [this](vwave::BeatAnalysis beats) {
            m_beats = std::move(beats);
            m_hasBeats = true;
            if (m_beats.bpm > 0.0) {
                std::ostringstream out;
                out << "Detected " << std::fixed << std::setprecision(1)
                    << m_beats.bpm << " BPM";
                m_status(out.str());
            } else {
                m_status("No tempo found");
            }
        });
}

void AnalysisController::detectPitch(
    std::shared_ptr<const vwave::Audio> audio, vwave::Range range) {
    if (!audio || audio->empty()) {
        m_status("Open an audio file first");
        return;
    }
    m_status("Detecting pitch...");
    m_service.requestPitch(
        std::move(audio), range,
        [this](vwave::PitchEstimate pitch) {
            m_pitch = std::move(pitch);
            m_hasPitch = true;
            if (m_pitch.frequency > 0.0) {
                std::ostringstream out;
                out << "Pitch " << std::fixed << std::setprecision(2)
                    << m_pitch.frequency << " Hz";
                m_status(out.str());
            } else {
                m_status("No stable pitch found");
            }
        });
}

} // namespace vwave::app
