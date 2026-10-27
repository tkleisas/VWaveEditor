#pragma once

#include "UiDispatcher.h"
#include "vwave/Audio.h"
#include "vwave/JobSystem.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>

namespace vwave::app {

class FileLoadController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using AudioSink =
        std::function<void(vwave::Audio audio, const std::string& path)>;
    using ProjectSink =
        std::function<void(vwave::Timeline timeline, const std::string& path)>;

    FileLoadController(vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                       StatusSink status);

    bool loadAudio(const std::string& path, AudioSink loaded);
    bool loadProject(const std::string& path, ProjectSink loaded);
    void cancel();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    StatusSink m_status;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
};

} // namespace vwave::app
