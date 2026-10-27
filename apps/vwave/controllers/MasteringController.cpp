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
                                         StatusSink status, Sink rendered)
    : m_timeline(timeline),
      m_player(player),
      m_status(std::move(status)),
      m_rendered(std::move(rendered)) {}

void MasteringController::clear() { m_analysis = {}; }

void MasteringController::cycleProfile() {
    m_profile = (m_profile + 1) % 6;
    m_analysis = {};
    m_status("Mastering profile: " +
             std::string(vwave::masteringProfileName(
                 static_cast<vwave::MasteringProfile>(m_profile))));
}

void MasteringController::toggleIncludeMuted() {
    m_includeMuted = !m_includeMuted;
    m_analysis = {};
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
    std::string error;
    m_analysis = vwave::analyzeMastering(m_timeline, options, &error);
    if (!m_analysis.valid) {
        m_status(error.empty() ? "Mastering analysis failed" : error);
        return;
    }
    m_status("Analyzed " + std::to_string(m_analysis.stems.size()) +
             " stems across " + std::to_string(m_analysis.bandCount) +
             " bands");
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
    std::string error;
    const vwave::MasteringRenderResult rendered =
        vwave::renderMastering(m_timeline, m_analysis, options, &error);
    if (!rendered.valid) {
        m_status(error.empty() ? "Mastering render failed" : error);
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
        const auto& original = m_timeline.track(stem.trackIndex);
        originals.push_back({original.startFrame, original.gainDb,
                             original.pan, original.solo, original.name,
                             stem.trackIndex});
    }
    m_player.stop();
    for (size_t i = 0; i < rendered.stems.size(); ++i) {
        const auto& stem = rendered.stems[i];
        const auto& info = originals[i];
        if (!m_timeline.addAudioTrack(info.name + " (mastered)", stem.audio,
                                      info.start, &error)) {
            m_status("Cannot add mastered stem: " + error);
            return;
        }
        const int index = m_timeline.trackCount() - 1;
        m_timeline.setTrackGainDb(index, info.gainDb);
        m_timeline.setTrackPan(index, info.pan);
        m_timeline.setTrackSolo(index, info.solo);
        if (m_muteOriginals) {
            m_timeline.setTrackMuted(info.index, true);
            m_timeline.setTrackSolo(info.index, false);
        }
    }
    m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
    m_rendered();
    std::ostringstream status;
    status << "Added " << rendered.stems.size() << " mastered stems";
    if (rendered.appliedGainDb != 0.0)
        status << "  ceiling gain " << ui::formatDb(rendered.appliedGainDb);
    m_status(status.str());
}

} // namespace vwave::app
