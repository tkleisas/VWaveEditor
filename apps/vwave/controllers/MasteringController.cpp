#include "controllers/MasteringController.h"

#include "services/FileService.h"
#include "ui/UiFormat.h"
#include "vwave/Mastering.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vwave::app {

MasteringController::MasteringController(vwave::Timeline& timeline,
                                         AudioPlayer& player,
                                         vwave::JobSystem& jobs,
                                         UiDispatcher& dispatcher,
                                         StatusSink status, Sink rendered)
    : m_timeline(timeline),
      m_player(player),
      m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_status(std::move(status)),
      m_rendered(std::move(rendered)) {}

void MasteringController::clear() {
    m_analysis = {};
    cancel();
}

void MasteringController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

void MasteringController::cycleProfile() {
    m_profile = (m_profile + 1) % 6;
    m_analysis = {};
    cancel();
    m_status("Mastering profile: " +
             std::string(vwave::masteringProfileName(
                 static_cast<vwave::MasteringProfile>(m_profile))));
}

void MasteringController::toggleIncludeMuted() {
    m_includeMuted = !m_includeMuted;
    m_analysis = {};
    cancel();
}

void MasteringController::toggleMuteOriginals() {
    m_muteOriginals = !m_muteOriginals;
}

void MasteringController::analyze(int bandCount, double maxCutDb,
                                  double maxBoostDb, int smoothingBands) {
    if (m_timeline.empty()) {
        m_status("Load stems or add tracks first");
        return;
    }
    vwave::MasteringOptions options;
    options.profile = static_cast<vwave::MasteringProfile>(
        std::clamp(m_profile, 0, 5));
    options.bandCount = bandCount;
    options.maxCutDb = maxCutDb;
    options.maxBoostDb = maxBoostDb;
    options.smoothingBands = smoothingBands;
    options.includeMuted = m_includeMuted;
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    const vwave::Timeline timeline = m_timeline;
    m_job = m_jobs.submit(
        [this, request, timeline, options](vwave::JobControl& control) mutable {
            std::string error;
            vwave::MasteringAnalysis analysis;
            try {
                analysis =
                    vwave::analyzeMastering(timeline, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, analysis = std::move(analysis),
                 error = std::move(error)]() mutable {
                    if (request != m_request) return;
                    if (!analysis.valid) {
                        m_status(error.empty() ? "Mastering analysis failed"
                                               : error);
                        return;
                    }
                    m_analysis = std::move(analysis);
                    m_status("Analyzed " +
                             std::to_string(m_analysis.stems.size()) +
                             " stems across " +
                             std::to_string(m_analysis.bandCount) + " bands");
                });
        });
    m_status("Analyzing stems...");
}

void MasteringController::saveReport(const std::string& path) const {
    if (!m_analysis.valid) {
        m_status("Analyze stems first");
        return;
    }
    std::string error;
    if (!vwave::saveMasteringAnalysis(m_analysis, path, &error)) {
        m_status(error.empty() ? "Mastering report save failed" : error);
        return;
    }
    m_status("Saved mastering report " + baseName(path));
}

void MasteringController::render(double ceilingDb) {
    if (!m_analysis.valid) {
        m_status("Analyze stems first");
        return;
    }
    vwave::MasteringProcessingOptions options;
    options.fftSize = 2048;
    options.hopSize = 512;
    options.limitMix = true;
    options.ceilingDb = ceilingDb;
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();
    const vwave::Timeline timeline = m_timeline;
    const vwave::MasteringAnalysis analysis = m_analysis;
    const bool muteOriginals = m_muteOriginals;
    m_job = m_jobs.submit(
        [this, request, timeline, analysis, options,
         muteOriginals](vwave::JobControl& control) mutable {
            std::string error;
            vwave::MasteringRenderResult rendered;
            try {
                rendered = vwave::renderMastering(timeline, analysis, options,
                                                  &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, rendered = std::move(rendered),
                 error = std::move(error), muteOriginals]() mutable {
                    if (request != m_request) return;
                    if (!rendered.valid) {
                        m_status(error.empty() ? "Mastering render failed"
                                               : error);
                        return;
                    }
                    struct OriginalInfo {
                        int64_t start = 0;
                        double gainDb = 0.0;
                        double pan = 0.0;
                        bool solo = false;
                        std::string name;
                        int index = -1;
                    };
                    std::vector<OriginalInfo> originals;
                    originals.reserve(rendered.stems.size());
                    for (const auto& stem : rendered.stems) {
                        if (stem.trackIndex < 0 ||
                            stem.trackIndex >= m_timeline.trackCount())
                            continue;
                        const auto& original =
                            m_timeline.track(stem.trackIndex);
                        originals.push_back(
                            {original.startFrame, original.gainDb, original.pan,
                             original.solo, original.name, stem.trackIndex});
                    }
                    if (originals.size() != rendered.stems.size()) {
                        m_status("Mastering render is stale");
                        return;
                    }
                    m_player.stop();
                    std::string addError;
                    for (size_t i = 0; i < rendered.stems.size(); ++i) {
                        const auto& stem = rendered.stems[i];
                        const auto& info = originals[i];
                        if (!m_timeline.addAudioTrack(
                                info.name + " (mastered)", stem.audio,
                                info.start, &addError)) {
                            m_status("Cannot add mastered stem: " + addError);
                            return;
                        }
                        const int index = m_timeline.trackCount() - 1;
                        m_timeline.setTrackGainDb(index, info.gainDb);
                        m_timeline.setTrackPan(index, info.pan);
                        m_timeline.setTrackSolo(index, info.solo);
                        if (muteOriginals) {
                            m_timeline.setTrackMuted(info.index, true);
                            m_timeline.setTrackSolo(info.index, false);
                        }
                    }
                    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
                    m_rendered();
                    std::ostringstream status;
                    status << "Added " << rendered.stems.size()
                           << " mastered stems";
                    if (rendered.appliedGainDb != 0.0)
                        status << "  ceiling gain "
                               << ui::formatDb(rendered.appliedGainDb);
                    m_status(status.str());
                });
        });
    m_status("Rendering mastered stems...");
}

} // namespace vwave::app
