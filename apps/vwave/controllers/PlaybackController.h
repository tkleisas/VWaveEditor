#pragma once

#include "AudioPlayer.h"
#include "controllers/LoopController.h"
#include "vwave/Timeline.h"

#include <cstdint>
#include <functional>
#include <string>

namespace vwave::app {

class PlaybackController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using FollowSink = std::function<void(int64_t)>;

    PlaybackController(vwave::Timeline& timeline, AudioPlayer& player,
                       int64_t& playhead, LoopController& loop,
                       StatusSink status, FollowSink follow);

    bool seekToTime(double seconds, std::string* error);
    void goToStart();
    void goToEnd();
    void togglePlay();
    void stop();
    void update();
    bool loopPlaybackActive() const;

private:
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    int64_t& m_playhead;
    LoopController& m_loop;
    StatusSink m_status;
    FollowSink m_follow;
};

} // namespace vwave::app
