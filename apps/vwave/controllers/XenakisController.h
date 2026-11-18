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
#include <vector>

namespace vwave::app {

class XenakisController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using RenderSink = std::function<void(int64_t start)>;

    XenakisController(vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                      vwave::Timeline& timeline, AudioPlayer& player,
                      StatusSink status, RenderSink rendered);

    bool valid() const { return !m_page.arcs.empty(); }
    bool hasPage() const { return !m_pageText.empty(); }
    const xen::Page& page() const { return m_page; }
    const std::string& pageText() const { return m_pageText; }
    const std::string& pageName() const { return m_pageName; }

    void generate(const vwave::XenakisGenerateOptions& options);
    void render(int64_t playhead, const vwave::XenakisRenderOptions& options);
    void renderFile(const std::string& path, int64_t playhead);
    bool openPageFile(const std::string& path);
    void preview(const vwave::XenakisRenderOptions& options);
    void stopPreview();
    bool previewing() const { return m_previewing || m_previewPending; }
    bool previewPlaying() const { return m_previewing; }
    int64_t previewPosition() const;
    int previewSampleRate() const { return m_previewSampleRate; }
    void cancel();

    void editPage(const std::function<void(xen::Page&)>& edit,
                  const std::string& coalesceKey = {});
    bool undoPage();
    bool redoPage();
    bool canUndoPage() const { return !m_undo.empty(); }
    bool canRedoPage() const { return !m_redo.empty(); }
    uint64_t pageRevision() const { return m_revision; }

private:
    bool setPageText(const std::string& text, const std::string& name,
                     bool resetHistory);
    void renderPage(const std::string& text,
                    const vwave::XenakisRenderOptions& options,
                    int64_t playhead, const std::string& name);
    void cancelJob();

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
    std::vector<std::string> m_undo;
    std::vector<std::string> m_redo;
    std::string m_lastEditKey;
    std::shared_ptr<vwave::Audio> m_previewAudio;
    bool m_previewing = false;
    bool m_previewPending = false;
    int m_previewSampleRate = 0;
    int64_t m_previewFrames = 0;
    uint64_t m_revision = 1;
};

} // namespace vwave::app
