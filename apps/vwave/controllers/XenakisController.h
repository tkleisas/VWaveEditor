#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/JobSystem.h"
#include "vwave/Timeline.h"

#include <cstdint>
#include <functional>
#include <string>

namespace vwave::app {

class XenakisController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using RenderSink = std::function<void(int64_t start)>;

    XenakisController(vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                      vwave::Timeline& timeline, AudioPlayer& player,
                      StatusSink status, RenderSink rendered);

    void renderFile(const std::string& path, int64_t playhead);
    void cancel();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    StatusSink m_status;
    RenderSink m_rendered;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
};

} // namespace vwave::app
