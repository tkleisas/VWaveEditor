#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/JobSystem.h"
#include "vwave/Mastering.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>

namespace vwave::app {

class MasteringController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using Sink = std::function<void()>;

    MasteringController(vwave::Timeline& timeline, AudioPlayer& player,
                        vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                        StatusSink status, Sink rendered);

    const vwave::MasteringAnalysis& analysis() const { return m_analysis; }
    bool valid() const { return m_analysis.valid; }
    int profile() const { return m_profile; }
    bool includeMuted() const { return m_includeMuted; }
    bool muteOriginals() const { return m_muteOriginals; }

    void clear();
    void cycleProfile();
    void toggleIncludeMuted();
    void toggleMuteOriginals();
    void analyze(int bandCount, double maxCutDb, double maxBoostDb,
                 int smoothingBands);
    void saveReport(const std::string& path) const;
    void render(double ceilingDb);

private:
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    StatusSink m_status;
    Sink m_rendered;
    vwave::MasteringAnalysis m_analysis;
    int m_profile = 0;
    bool m_includeMuted = false;
    bool m_muteOriginals = true;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;

    void cancel();
};

} // namespace vwave::app
