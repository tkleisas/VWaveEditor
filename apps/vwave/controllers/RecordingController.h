#pragma once

#include "AudioPlayer.h"
#include "AudioRecorder.h"
#include "vwave/Timeline.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vwave::app {

class RecordingController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using Sink = std::function<void()>;

    RecordingController(vwave::Timeline& timeline, AudioPlayer& player,
                        int64_t& playhead, StatusSink status,
                        Sink trackChanged, Sink recordingStored);
    ~RecordingController();

    bool recording() const { return m_recording; }
    int64_t capturedFrames() const {
        return m_recordedChannels > 0
                   ? static_cast<int64_t>(m_recordedInterleaved.size()) /
                         m_recordedChannels
                   : 0;
    }
    int capturedSampleRate() const { return m_recordedSampleRate; }
    void toggle(const std::string& inputDevice);
    void start(const std::string& inputDevice);
    void update();
    void stop();

private:
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    int64_t& m_playhead;
    AudioRecorder m_recorder;
    StatusSink m_status;
    Sink m_trackChanged;
    Sink m_recordingStored;
    bool m_recording = false;
    int64_t m_recordStartFrame = 0;
    int m_recordTarget = -1;
    std::vector<float> m_recordedInterleaved;
    int m_recordedChannels = 0;
    int m_recordedSampleRate = 0;
};

} // namespace vwave::app
