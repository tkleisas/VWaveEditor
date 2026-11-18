#include "controllers/XenakisController.h"

#include "ui/UiFormat.h"
#include "vwave/Recipe.h"
#include "vwave/Xenakis.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace vwave::app {

XenakisController::XenakisController(vwave::JobSystem& jobs,
                                     UiDispatcher& dispatcher,
                                     vwave::Timeline& timeline,
                                     AudioPlayer& player, StatusSink status,
                                     RenderSink rendered)
    : m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_timeline(timeline),
      m_player(player),
      m_status(std::move(status)),
      m_rendered(std::move(rendered)) {}

void XenakisController::renderFile(const std::string& path,
                                   int64_t playhead) {
    const int64_t start =
        m_timeline.empty()
            ? 0
            : ui::clampFrame(playhead, m_timeline.lengthFrames());
    const int sampleRate =
        m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000;
    const std::string name =
        std::filesystem::path(path).stem().string();
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    m_job = m_jobs.submit(
        [this, request, path, sampleRate, start, name](
            vwave::JobControl& control) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                m_dispatcher.post([this, request]() {
                    if (request == m_request)
                        m_status("Cannot open Xenakis page");
                });
                return;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            const std::string pageText = buffer.str();

            vwave::XenakisRenderOptions options;
            options.sampleRate = sampleRate;
            options.gain = 1.0;
            options.normalize = true;
            options.peak = 0.9;

            std::string error;
            vwave::Audio rendered;
            try {
                rendered =
                    vwave::renderXenakisPage(pageText, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;

            m_dispatcher.post(
                [this, request, rendered = std::move(rendered),
                 error = std::move(error), pageText, options, start,
                 name]() mutable {
                    if (request != m_request) return;
                    if (rendered.empty()) {
                        m_status(error.empty() ? "Xenakis render failed"
                                               : error);
                        return;
                    }
                    m_player.stop();
                    std::string addError;
                    const std::string trackName =
                        name.empty() ? std::string("Xenakis") : name;
                    if (!m_timeline.addAudioTrack(trackName, rendered,
                                                  start, &addError)) {
                        m_status("Cannot add Xenakis track: " + addError);
                        return;
                    }
                    const int trackIndex = m_timeline.trackCount() - 1;
                    m_timeline.track(trackIndex).recipe =
                        vwave::makeXenakisTrackRecipe(pageText, options);
                    m_timeline.setActiveTrack(trackIndex);
                    m_rendered(start);
                    m_status("Rendered Xenakis page to track (" +
                             ui::formatSeconds(rendered.duration()) + ")");
                });
        });
    m_status("Rendering Xenakis page...");
}

void XenakisController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
