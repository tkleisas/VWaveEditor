#include "services/FileSaveController.h"

#include "services/FileService.h"
#include "ui/UiFormat.h"
#include "vwave/AudioIO.h"

#include <cmath>
#include <filesystem>
#include <utility>

namespace vwave::app {

FileSaveController::FileSaveController(vwave::JobSystem& jobs,
                                       UiDispatcher& dispatcher,
                                       StatusSink status)
    : m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_status(std::move(status)) {}

void FileSaveController::saveAudio(vwave::Timeline timeline,
                                   const std::string& path, PathSink saved) {
    const std::string target = withExtension(path, ".wav");
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    m_status("Exporting audio...");
    m_job = m_jobs.submit([this, request, timeline = std::move(timeline),
                           target, saved = std::move(saved)](
                              vwave::JobControl& control) mutable {
        vwave::Audio mix = timeline.mixdown();
        std::string error;
        bool success = false;
        const float peak = mix.empty() ? 0.0f : mix.peak();
        if (!mix.empty())
            success = vwave::writeAudio(target, mix, {}, &error);
        if (control.cancelled()) return;
        m_dispatcher.post([this, request, success, error = std::move(error),
                           target, peak, saved = std::move(saved)]() mutable {
            if (request != m_request) return;
            if (!success) {
                m_status(error.empty() ? "Nothing to export"
                                       : "Save failed: " + error);
                return;
            }
            saved(target);
            if (peak > 1.0f) {
                const double peakDb =
                    20.0 * std::log10(static_cast<double>(peak));
                m_status("Saved " + baseName(target) + " (peak " +
                         ui::formatDb(peakDb) +
                         "; integer PCM may clip)");
            } else {
                m_status("Saved " + baseName(target));
            }
        });
    });
}

void FileSaveController::saveProject(vwave::Timeline timeline,
                                     const std::string& path, PathSink saved) {
    const std::string target = withExtension(path, ".vwdoc");
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    m_status("Saving project...");
    m_job = m_jobs.submit([this, request, timeline = std::move(timeline),
                           target, saved = std::move(saved)](
                              vwave::JobControl& control) mutable {
        std::string error;
        const bool success = timeline.saveProject(target, &error);
        if (control.cancelled()) return;
        m_dispatcher.post([this, request, success, error = std::move(error),
                           target, saved = std::move(saved)]() mutable {
            if (request != m_request) return;
            if (!success) {
                m_status("Save failed: " + error);
                return;
            }
            saved(target);
            m_status("Saved project " + baseName(target));
        });
    });
}

void FileSaveController::exportSlices(vwave::Audio audio,
                                      std::vector<vwave::Slice> slices,
                                      const std::string& directory,
                                      std::string prefix, SliceSink saved) {
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    m_status("Exporting slices...");
    m_job = m_jobs.submit(
        [this, request, audio = std::move(audio),
         slices = std::move(slices), directory, prefix = std::move(prefix),
         saved = std::move(saved)](vwave::JobControl& control) mutable {
            vwave::SliceExportOptions options;
            options.prefix = std::move(prefix);
            vwave::SliceExportResult result;
            std::string error;
            const bool success = vwave::exportSlices(
                audio, slices, directory, options, &result, &error);
            if (control.cancelled()) return;
            m_dispatcher.post([this, request, success,
                               error = std::move(error), directory,
                               count = result.files.size(),
                               saved = std::move(saved)]() mutable {
                if (request != m_request) return;
                if (!success) {
                    m_status(error.empty() ? "Export failed"
                                           : "Export failed: " + error);
                    return;
                }
                saved(count, directory);
                m_status("Exported " + std::to_string(count) + " slices");
            });
        });
}

void FileSaveController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
