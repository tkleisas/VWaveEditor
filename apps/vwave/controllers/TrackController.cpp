#include "controllers/TrackController.h"

#include <algorithm>
#include <utility>

namespace vwave::app {

TrackController::TrackController(vwave::Timeline& timeline,
                                 AudioPlayer& player, int64_t& playhead,
                                 Sink selectionChanged, Sink tracksChanged,
                                 StatusSink status)
    : m_timeline(timeline),
      m_player(player),
      m_playhead(playhead),
      m_selectionChanged(std::move(selectionChanged)),
      m_tracksChanged(std::move(tracksChanged)),
      m_status(std::move(status)) {}

void TrackController::syncControls() {
    const size_t count = static_cast<size_t>(m_timeline.trackCount());
    m_controls.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& track = m_timeline.track(static_cast<int>(i));
        m_controls[i].gain = {-60.0, 12.0, 0.0, false, 0.5};
        m_controls[i].gain.setValue(track.gainDb);
        m_controls[i].pan = {-1.0, 1.0, 0.0, false, 0.05};
        m_controls[i].pan.setValue(track.pan);
    }
}

vwave::ParameterSlider& TrackController::gainControl(int index) {
    return m_controls[static_cast<size_t>(index)].gain;
}

vwave::ParameterSlider& TrackController::panControl(int index) {
    return m_controls[static_cast<size_t>(index)].pan;
}

void TrackController::select(int index) {
    if (index < 0 || index >= m_timeline.trackCount()) return;
    if (m_timeline.activeTrack() == index) return;
    m_timeline.setActiveTrack(index);
    m_selectionChanged();
    m_status("Track: " + m_timeline.track(index).name);
}

void TrackController::remove(int index) {
    if (index < 0 || index >= m_timeline.trackCount()) return;
    m_player.stop();
    const std::string name = m_timeline.track(index).name;
    if (!m_timeline.removeTrack(index)) return;
    syncControls();
    m_tracksChanged();
    m_status("Removed track " + name);
}

void TrackController::addEmpty() {
    m_player.stop();
    const int64_t start =
        std::clamp<int64_t>(m_playhead, 0, m_timeline.lengthFrames());
    std::string error;
    if (!m_timeline.addEmptyTrack(
            "Track " + std::to_string(m_timeline.trackCount() + 1), start,
            &error)) {
        m_status("Cannot add track: " + error);
        return;
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    for (int i = 0; i < m_timeline.trackCount(); ++i)
        m_timeline.setTrackArmed(i, i == m_timeline.activeTrack());
    syncControls();
    m_tracksChanged();
    m_status("Added empty track");
}

} // namespace vwave::app
