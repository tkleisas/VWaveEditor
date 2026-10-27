#include "controllers/StereoController.h"

#include <string>
#include <utility>

namespace vwave::app {

void StereoController::cyclePadding() {
    switch (m_padding) {
    case vwave::StereoPadding::Back:
        m_padding = vwave::StereoPadding::Front;
        break;
    case vwave::StereoPadding::Front:
        m_padding = vwave::StereoPadding::Both;
        break;
    case vwave::StereoPadding::Both:
        m_padding = vwave::StereoPadding::Back;
        break;
    }
    m_status("Stereo padding: " +
             std::string(vwave::stereoPaddingName(m_padding)));
}

void StereoController::splitActive() {
    if (m_timeline.empty()) return;
    const int index = m_timeline.activeTrack();
    if (m_timeline.track(index).document.numChannels() != 2) {
        m_status("Active track is not stereo");
        return;
    }
    m_player.stop();
    std::string error;
    if (!m_timeline.splitStereoTrack(index, true, &error)) {
        m_status(error.empty() ? "Split failed" : error);
        return;
    }
    m_changed();
    m_status("Split stereo track into mono tracks");
}

void StereoController::combineActive(int direction) {
    if (m_timeline.empty()) return;
    const int active = m_timeline.activeTrack();
    const int other = active + direction;
    if (active < 0 || other < 0 || other >= m_timeline.trackCount()) {
        m_status("No adjacent track to combine");
        return;
    }
    if (m_timeline.track(active).document.numChannels() != 1 ||
        m_timeline.track(other).document.numChannels() != 1) {
        m_status("Both tracks must be mono");
        return;
    }
    m_player.stop();
    std::string error;
    vwave::StereoCombineOptions options;
    options.padding = m_padding;
    options.keepOriginals = true;
    if (!m_timeline.combineMonoTracks(
            direction < 0 ? other : active,
            direction < 0 ? active : other, options, &error)) {
        m_status(error.empty() ? "Combine failed" : error);
        return;
    }
    m_changed();
    m_status("Combined mono tracks with " +
             std::string(vwave::stereoPaddingName(m_padding)) + " padding");
}

} // namespace vwave::app
