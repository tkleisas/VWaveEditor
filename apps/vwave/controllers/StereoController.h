#pragma once

#include "AudioPlayer.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>
#include <utility>

namespace vwave::app {

class StereoController {
public:
    using ChangedSink = std::function<void()>;
    using StatusSink = std::function<void(const std::string&)>;

    StereoController(vwave::Timeline& timeline, AudioPlayer& player,
                     ChangedSink changed, StatusSink status)
        : m_timeline(timeline),
          m_player(player),
          m_changed(std::move(changed)),
          m_status(std::move(status)) {}

    vwave::StereoPadding padding() const { return m_padding; }
    void cyclePadding();
    void splitActive();
    void combineActive(int direction);

private:
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    ChangedSink m_changed;
    StatusSink m_status;
    vwave::StereoPadding m_padding = vwave::StereoPadding::Back;
};

} // namespace vwave::app
