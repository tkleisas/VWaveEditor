#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/JobSystem.h"
#include "vwave/Timeline.h"
#include "vwave/Xenakis.h"

#include <xen/upic.h>

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

    bool valid() const { return !m_page.arcs.empty(); }
    const xen::Page& page() const { return m_page; }
    const std::string& pageText() const { return m_pageText; }
    const std::string& pageName() const { return m_pageName; }

    void generate(const vwave::XenakisGenerateOptions& options);
    void render(int64_t playhead, const vwave::XenakisRenderOptions& options);
    void renderFile(const std::string& path, int64_t playhead);
    void cancel();

private:
    bool setPageText(const std::string& text, const std::string& name);
    void renderPage(const std::string& text,
                    const vwave::XenakisRenderOptions& options,
                    int64_t playhead, const std::string& name);

    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    StatusSink m_status;
    RenderSink m_rendered;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
    xen::Page m_page;
    std::string m_pageText;
    std::string m_pageName = "Xenakis";
};

} // namespace vwave::app
