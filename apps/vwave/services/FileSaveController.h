#pragma once

#include "UiDispatcher.h"
#include "vwave/Analysis.h"
#include "vwave/Audio.h"
#include "vwave/JobSystem.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>
#include <vector>

namespace vwave::app {

class FileSaveController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using PathSink = std::function<void(const std::string& path)>;
    using SliceSink = std::function<void(size_t count,
                                         const std::string& directory)>;

    FileSaveController(vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                       StatusSink status);

    void saveAudio(vwave::Timeline timeline, const std::string& path,
                   PathSink saved);
    void saveProject(vwave::Timeline timeline, const std::string& path,
                     PathSink saved);
    void exportSlices(vwave::Audio audio, std::vector<vwave::Slice> slices,
                      const std::string& directory, std::string prefix,
                      SliceSink saved);
    void cancel();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    StatusSink m_status;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
};

} // namespace vwave::app
