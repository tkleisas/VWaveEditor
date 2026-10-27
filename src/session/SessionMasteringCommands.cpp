#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Mastering.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeMasteringCommands(const std::string& command,
                                                const nlohmann::json& params,
                                                std::string* error) {
        if (command == "mastering_analysis") {
            MasteringOptions options;
            if (!parseMasteringOptions(params, options, error))
                return nullptr;
            const MasteringAnalysis analysis =
                analyzeMastering(m_timeline, options, error);
            if (!analysis.valid) return nullptr;
            nlohmann::json result = masteringAnalysisToJson(analysis);
            if (params.contains("out")) {
                const std::string out = requiredString(params, "out", error);
                if (out.empty()) return nullptr;
                if (!saveMasteringAnalysis(analysis, out, error))
                    return nullptr;
                result["path"] = out;
            }
            return result;
        }

        if (command == "mastering_render") {
            MasteringOptions analysisOptions;
            if (!parseMasteringOptions(params, analysisOptions, error))
                return nullptr;
            MasteringProcessingOptions processing;
            processing.fftSize =
                optionalInt(params, "process_fft_size", 2048);
            processing.hopSize =
                optionalInt(params, "process_hop_size", 512);
            processing.limitMix =
                optionalBool(params, "limit_mix", true);
            processing.ceilingDb =
                optionalDouble(params, "ceiling_db", -1.0);
            const MasteringAnalysis analysis =
                analyzeMastering(m_timeline, analysisOptions, error);
            if (!analysis.valid) return nullptr;
            const MasteringRenderResult rendered =
                renderMastering(m_timeline, analysis, processing, error);
            if (!rendered.valid) return nullptr;

            nlohmann::json result;
            result["ok"] = true;
            result["profile"] =
                masteringProfileName(analysis.profile);
            result["summary"] = rendered.summary;
            result["applied_gain_db"] = rendered.appliedGainDb;
            result["mix_peak_db"] = rendered.mixPeakDb;
            result["mix_true_peak_db"] = rendered.mixTruePeakDb;
            result["analysis"] = masteringAnalysisToJson(analysis);
            result["stems"] = nlohmann::json::array();
            for (size_t i = 0; i < rendered.stems.size(); ++i) {
                const auto& stem = rendered.stems[i];
                result["stems"].push_back(
                    {{"track_index", stem.trackIndex},
                     {"name", stem.name},
                     {"frames", stem.audio.numFrames()},
                     {"sample_rate", stem.audio.sampleRate()},
                     {"min_cut_db", stem.minCutDb},
                     {"max_boost_db", stem.maxBoostDb}});
            }

            if (params.contains("out_dir")) {
                const std::string directory =
                    requiredString(params, "out_dir", error);
                if (directory.empty()) return nullptr;
                std::error_code ec;
                std::filesystem::create_directories(directory, ec);
                if (ec) {
                    setError(error, "cannot create mastering output directory");
                    return nullptr;
                }
                const std::string prefix =
                    params.value("prefix", std::string{"mastered"});
                AudioExportOptions exportOptions =
                    exportOptionsFrom(params, error);
                if (error && !error->empty()) return nullptr;
                exportOptions.format = AudioFormat::Wav;
                for (size_t i = 0; i < rendered.stems.size(); ++i) {
                    const auto& stem = rendered.stems[i];
                    const std::string path =
                        (std::filesystem::path(directory) /
                         (prefix + "_stem_" + std::to_string(i + 1) +
                          ".wav")).string();
                    if (!writeAudio(path, stem.audio, exportOptions, error))
                        return nullptr;
                    result["stems"][i]["path"] = path;
                }
                const std::string mixPath =
                    (std::filesystem::path(directory) /
                     (prefix + "_mix.wav")).string();
                if (!writeAudio(mixPath, rendered.mix, exportOptions, error))
                    return nullptr;
                result["mix_path"] = mixPath;
                const std::string reportPath =
                    (std::filesystem::path(directory) /
                     (prefix + "_mastering.json")).string();
                {
                    std::ofstream out(reportPath, std::ios::binary);
                    if (!out) {
                        setError(error, "cannot create mastering manifest");
                        return nullptr;
                    }
                    out << result.dump(2) << '\n';
                }
                result["manifest"] = reportPath;
            }

            if (optionalBool(params, "add_tracks", false)) {
                const bool muteOriginals =
                    optionalBool(params, "mute_originals", false);
                result["added_tracks"] = nlohmann::json::array();
                for (const auto& stem : rendered.stems) {
                    const auto& original = m_timeline.track(stem.trackIndex);
                    const std::string name =
                        original.name + " (mastered)";
                    const int64_t start = original.startFrame;
                    const double gain = original.gainDb;
                    const double pan = original.pan;
                    const bool solo = original.solo;
                    if (!m_timeline.addAudioTrack(name, stem.audio, start,
                                                  error))
                        return nullptr;
                    const int index = m_timeline.trackCount() - 1;
                    m_timeline.setTrackGainDb(index, gain);
                    m_timeline.setTrackPan(index, pan);
                    m_timeline.setTrackSolo(index, solo);
                    if (muteOriginals) {
                        m_timeline.setTrackMuted(stem.trackIndex, true);
                        m_timeline.setTrackSolo(stem.trackIndex, false);
                    }
                    result["added_tracks"].push_back(index);
                }
                m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
            }
            return result;
        }

    setError(error, "unknown command: " + command);
    return nullptr;
}

} // namespace vwave
