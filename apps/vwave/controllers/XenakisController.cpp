#include "controllers/XenakisController.h"

#include "ui/UiFormat.h"
#include "vwave/Recipe.h"

#include <xen/page_model.h>

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

bool XenakisController::setPageText(const std::string& text,
                                    const std::string& name) {
    xen::Page page;
    if (!xen::deserializePage(text, page) || page.arcs.empty()) {
        m_status("Invalid Xenakis page");
        return false;
    }
    const std::string internalName = page.name;
    m_pageText = text;
    m_page = std::move(page);
    m_pageName = !name.empty()
                     ? name
                     : (internalName.empty() ? std::string("Xenakis")
                                             : internalName);
    return true;
}

void XenakisController::generate(
    const vwave::XenakisGenerateOptions& options) {
    std::string error;
    const std::string text = vwave::generateXenakisPage(options, &error);
    if (text.empty()) {
        m_status(error.empty() ? "Xenakis generation failed" : error);
        return;
    }
    if (!setPageText(text, {})) return;
    std::ostringstream status;
    status << "Generated " << m_pageName << ": " << m_page.arcs.size()
           << " arcs, " << ui::formatSeconds(m_page.duration);
    m_status(status.str());
}

void XenakisController::render(
    int64_t playhead, const vwave::XenakisRenderOptions& options) {
    if (m_pageText.empty()) {
        m_status("Generate or open a Xenakis page first");
        return;
    }
    renderPage(m_pageText, options, playhead, m_pageName);
}

void XenakisController::renderFile(const std::string& path,
                                   int64_t playhead) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        m_status("Cannot open Xenakis page");
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string name =
        std::filesystem::path(path).stem().string();
    if (!setPageText(buffer.str(), name)) return;

    vwave::XenakisRenderOptions options;
    options.sampleRate =
        m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000;
    options.gain = 1.0;
    options.normalize = true;
    options.peak = 0.9;
    renderPage(m_pageText, options, playhead, m_pageName);
}

void XenakisController::renderPage(
    const std::string& text, const vwave::XenakisRenderOptions& options,
    int64_t playhead, const std::string& name) {
    const int64_t start =
        m_timeline.empty()
            ? 0
            : ui::clampFrame(playhead, m_timeline.lengthFrames());
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    m_job = m_jobs.submit(
        [this, request, text, options, start, name](
            vwave::JobControl& control) {
            std::string error;
            vwave::Audio rendered;
            try {
                rendered = vwave::renderXenakisPage(text, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;

            m_dispatcher.post(
                [this, request, rendered = std::move(rendered),
                 error = std::move(error), text, options, start,
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
                        vwave::makeXenakisTrackRecipe(text, options);
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
