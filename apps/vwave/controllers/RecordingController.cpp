#include "controllers/RecordingController.h"

#include "ui/UiFormat.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vwave::app {

RecordingController::RecordingController(vwave::Timeline& timeline,
                                         AudioPlayer& player,
                                         int64_t& playhead, StatusSink status,
                                         Sink trackChanged,
                                         Sink recordingStored)
    : m_timeline(timeline),
      m_player(player),
      m_playhead(playhead),
      m_status(std::move(status)),
      m_trackChanged(std::move(trackChanged)),
      m_recordingStored(std::move(recordingStored)) {}

RecordingController::~RecordingController() { m_recorder.stop(); }

void RecordingController::toggle(const std::string& inputDevice) {
    if (m_recording)
        stop();
    else
        start(inputDevice);
}

void RecordingController::start(const std::string& inputDevice) {
    if (m_recording) return;
    m_player.stop();
    const int64_t recordStart = std::clamp<int64_t>(
        m_playhead, 0, m_timeline.lengthFrames());
    int target = m_timeline.armedTrack();
    bool created = false;
    if (target < 0 || !m_timeline.track(target).empty) {
        std::string name = "Rec " + std::to_string(m_timeline.trackCount() + 1);
        if (target >= 0) name = m_timeline.track(target).name + " take";
        std::string error;
        if (!m_timeline.addEmptyTrack(name, recordStart, &error)) {
            m_status("Cannot create recording track: " + error);
            return;
        }
        target = m_timeline.trackCount() - 1;
        created = true;
    }
    if (target < 0) {
        m_status("No recording track");
        return;
    }
    m_timeline.setActiveTrack(target);
    for (int i = 0; i < m_timeline.trackCount(); ++i)
        m_timeline.setTrackArmed(i, i == target);
    m_timeline.setTrackStartFrame(target, recordStart);
    m_trackChanged();
    m_recordTarget = target;
    m_recordStartFrame = recordStart;
    m_recordedInterleaved.clear();
    m_recording = true;
    const char* device = inputDevice.empty() ? nullptr : inputDevice.c_str();
    if (!m_recorder.start(device, m_timeline.sampleRate())) {
        m_recording = false;
        m_status("Cannot open audio input");
        if (created) {
            m_timeline.removeTrack(target);
            m_trackChanged();
        }
        return;
    }
    m_recordedChannels = m_recorder.channels();
    m_recordedSampleRate = m_recorder.sampleRate();
    m_status("Recording... press Record or F9 to stop");
}

void RecordingController::update() {
    if (m_recording) m_recorder.drain(m_recordedInterleaved);
}

void RecordingController::stop() {
    if (!m_recording) return;
    m_recorder.drain(m_recordedInterleaved);
    m_recorder.stop();
    m_recording = false;
    if (m_recordedChannels <= 0 || m_recordedSampleRate <= 0) {
        m_recordedInterleaved.clear();
        m_status("Recording stopped");
        return;
    }
    const int64_t frames = static_cast<int64_t>(
        m_recordedInterleaved.size() /
        static_cast<size_t>(m_recordedChannels));
    if (frames <= 0) {
        m_recordedInterleaved.clear();
        m_status("Recording stopped - no input captured");
        return;
    }
    vwave::Audio recorded = vwave::Audio::fromInterleaved(
        m_recordedInterleaved.data(), m_recordedChannels, frames,
        m_recordedSampleRate);
    m_recordedInterleaved.clear();
    std::string error;
    int target = m_recordTarget;
    if (target < 0 || target >= m_timeline.trackCount()) {
        if (!m_timeline.addAudioTrack("Recording", recorded,
                                      m_recordStartFrame, &error)) {
            m_status("Cannot store recording: " + error);
            return;
        }
        target = m_timeline.trackCount() - 1;
    } else if (m_timeline.track(target).empty) {
        if (!m_timeline.setTrackAudio(target, std::move(recorded),
                                      m_recordStartFrame, &error)) {
            m_status("Cannot store recording: " + error);
            return;
        }
    } else {
        if (!m_timeline.addAudioTrack(m_timeline.track(target).name + " take",
                                      recorded, m_recordStartFrame, &error)) {
            m_status("Cannot store recording: " + error);
            return;
        }
        target = m_timeline.trackCount() - 1;
    }
    m_timeline.setActiveTrack(target);
    m_playhead = m_recordStartFrame;
    m_recordTarget = -1;
    m_recordingStored();
    m_status("Recorded " +
             ui::formatSeconds(static_cast<double>(frames) /
                               std::max(1, m_recordedSampleRate)));
}

} // namespace vwave::app
