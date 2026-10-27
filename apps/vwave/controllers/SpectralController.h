#pragma once

#include "AudioPlayer.h"
#include "vwave/Spectral.h"
#include "vwave/UiModel.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>

namespace vwave::app {

class SpectralController {
public:
    using EditRunner = std::function<void(
        const std::function<bool(std::string*)>&, const std::string&)>;
    using StatusSink = std::function<void(const std::string&)>;

    SpectralController(vwave::Timeline& timeline, AudioPlayer& player,
                       EditRunner runEdit, StatusSink status);

    vwave::SpectralRegion& region() { return m_region; }
    const vwave::SpectralRegion& region() const { return m_region; }
    void clear();

    vwave::Range timeRange() const;
    void applyGain(double db);
    void applyFilter(bool lowpass, double cutoffHz);
    void applyGate(double thresholdDb);
    void applyBlur(int timeRadius, int frequencyRadius);

private:
    vwave::Document* activeDocument();

    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    EditRunner m_runEdit;
    StatusSink m_status;
    vwave::SpectralRegion m_region;
};

} // namespace vwave::app
