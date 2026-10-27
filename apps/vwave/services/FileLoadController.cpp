#include "services/FileLoadController.h"

#include "vwave/AudioIO.h"

#include <utility>

namespace vwave::app {

FileLoadController::FileLoadController(vwave::JobSystem& jobs,
                                       UiDispatcher& dispatcher,
                                       StatusSink status)
    : m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_status(std::move(status)) {}

bool FileLoadController::loadAudio(const std::string& path, AudioSink loaded) {
    if (path.empty()) {
        m_status("Audio path is empty");
        return false;
    }
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    m_status("Loading audio...");
    m_job = m_jobs.submit([this, request, path,
                           loaded = std::move(loaded)](
                              vwave::JobControl& control) mutable {
        vwave::Audio audio;
        std::string error;
        bool success = false;
        try {
            success = vwave::readAudio(path, audio, &error);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
        if (control.cancelled()) return;
        m_dispatcher.post([this, request, audio = std::move(audio), success,
                           error = std::move(error), path,
                           loaded = std::move(loaded)]() mutable {
            if (request != m_request) return;
            if (!success || audio.empty()) {
                m_status(error.empty() ? "Open failed" : "Open failed: " + error);
                return;
            }
            loaded(std::move(audio), path);
        });
    });
    return true;
}

bool FileLoadController::loadProject(const std::string& path,
                                     ProjectSink loaded) {
    if (path.empty()) {
        m_status("Project path is empty");
        return false;
    }
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    m_status("Loading project...");
    m_job = m_jobs.submit([this, request, path,
                           loaded = std::move(loaded)](
                              vwave::JobControl& control) mutable {
        vwave::Timeline timeline;
        std::string error;
        bool success = false;
        try {
            success = timeline.loadProject(path, &error);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
        if (control.cancelled()) return;
        m_dispatcher.post([this, request, timeline = std::move(timeline),
                           success, error = std::move(error), path,
                           loaded = std::move(loaded)]() mutable {
            if (request != m_request) return;
            if (!success) {
                m_status(error.empty() ? "Open failed" : "Open failed: " + error);
                return;
            }
            loaded(std::move(timeline), path);
        });
    });
    return true;
}

void FileLoadController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
