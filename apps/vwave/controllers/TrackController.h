#pragma once

#include "AudioPlayer.h"
#include "ui/Widgets.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>
#include <vector>

namespace vwave::app {

class TrackController {
public:
    using Sink = std::function<void()>;
    using StatusSink = std::function<void(const std::string&)>;

    TrackController(vwave::Timeline& timeline, AudioPlayer& player,
                    int64_t& playhead, Sink selectionChanged,
                    Sink tracksChanged, StatusSink status);

    void syncControls();
    vwave::ParameterSlider& gainControl(int index);
    vwave::ParameterSlider& panControl(int index);

    void select(int index);
    void remove(int index);
    void addEmpty();

private:
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    int64_t& m_playhead;
    Sink m_selectionChanged;
    Sink m_tracksChanged;
    StatusSink m_status;
    std::vector<vwave::ui::TrackControls> m_controls;
};

} // namespace vwave::app
