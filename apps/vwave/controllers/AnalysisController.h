#pragma once

#include "services/AnalysisService.h"

#include "vwave/Analysis.h"
#include "vwave/Audio.h"

#include <functional>
#include <memory>

namespace vwave::app {

class AnalysisController {
public:
    using StatusSink = std::function<void(const std::string&)>;

    AnalysisController(AnalysisService& service, StatusSink status)
        : m_service(service), m_status(std::move(status)) {}

    const vwave::BeatAnalysis& beats() const { return m_beats; }
    bool hasBeats() const { return m_hasBeats; }
    const vwave::PitchEstimate& pitch() const { return m_pitch; }
    bool hasPitch() const { return m_hasPitch; }

    void clear();
    void analyzeBeats(std::shared_ptr<const vwave::Audio> audio,
                      double sensitivity);
    void detectPitch(std::shared_ptr<const vwave::Audio> audio,
                     vwave::Range range);

private:
    AnalysisService& m_service;
    StatusSink m_status;
    vwave::BeatAnalysis m_beats;
    vwave::PitchEstimate m_pitch;
    bool m_hasBeats = false;
    bool m_hasPitch = false;
};

} // namespace vwave::app
