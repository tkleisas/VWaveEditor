#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Analysis.h"

#include <string>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeAnalysisCommands(const std::string& command,
                                                const nlohmann::json& params,
                                                std::string* error) {
        if (command == "analyze_beats") {
            const BeatAnalysis analysis = analyzeBeats(
                document().audio(), optionalDouble(params, "sensitivity", 0.5));
            nlohmann::json result;
            result["ok"] = true;
            result["bpm"] = analysis.bpm;
            result["confidence"] = analysis.confidence;
            result["first_beat_seconds"] = analysis.firstBeatSeconds;
            result["onsets"] = nlohmann::json::array();
            for (const Onset& onset : analysis.onsets)
                result["onsets"].push_back(
                    {{"frame", onset.frame},
                     {"time", static_cast<double>(onset.frame) /
                                  document().sampleRate()},
                     {"strength", onset.strength}});
            result["beats"] = nlohmann::json::array();
            for (int64_t beat : analysis.beats) {
                result["beats"].push_back(
                    {{"frame", beat},
                     {"time", static_cast<double>(beat) /
                                  document().sampleRate()}});
            }
            return result;
        }
        if (command == "detect_pitch") {
            Range range;
            bool hasRange = false;
            if (!parseOptionalRange(document().audio(), params, range,
                                    hasRange, error))
                return nullptr;
            PitchOptions options;
            options.minHz = optionalDouble(params, "min_hz", 40.0);
            options.maxHz = optionalDouble(params, "max_hz", 2000.0);
            const PitchEstimate estimate =
                detectPitch(document().audio(), hasRange ? range : Range{},
                            options);
            return {{"ok", true},
                    {"frequency", estimate.frequency},
                    {"confidence", estimate.confidence},
                    {"frame", estimate.frame},
                    {"time", document().sampleRate() > 0
                                 ? static_cast<double>(estimate.frame) /
                                       document().sampleRate()
                                 : 0.0}};
        }

        if (command == "slices") {
            const std::string mode =
                params.value("mode", std::string{"transients"});
            std::vector<Slice> slices;
            if (mode == "transients") {
                slices = sliceByTransients(
                    document().audio(),
                    optionalDouble(params, "sensitivity", 0.5),
                    optionalDouble(params, "min_slice_seconds", 0.03));
            } else if (mode == "beats") {
                double bpm = optionalDouble(params, "bpm", 0.0);
                if (bpm <= 0.0)
                    bpm = analyzeBeats(document().audio()).bpm;
                slices = sliceByBeats(
                    document().audio(), bpm,
                    optionalInt(params, "beats_per_slice", 1));
            } else if (mode == "markers") {
                std::vector<int64_t> markers;
                for (const Marker& marker : document().markers())
                    markers.push_back(marker.frame);
                slices = sliceByMarkers(document().audio(), markers);
            } else {
                setError(error, "unknown slice mode: " + mode);
                return nullptr;
            }
            if (slices.empty()) {
                setError(error, "no slices were produced");
                return nullptr;
            }
            document().setSlices(slices);
            nlohmann::json result;
            result["ok"] = true;
            result["slices"] = nlohmann::json::array();
            for (const Slice& slice : slices)
                result["slices"].push_back(
                    sliceToJson(slice, document().sampleRate()));
            return result;
        }
        if (command == "export_slices") {
            const std::string directory =
                requiredString(params, "directory", error);
            if (directory.empty()) return nullptr;
            SliceExportOptions options;
            options.prefix = params.value("prefix", std::string{"slice"});
            options.audio = exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            SliceExportResult result;
            if (!exportSlices(document().audio(), document().slices(),
                              directory, options, &result, error))
                return nullptr;
            nlohmann::json json;
            json["ok"] = true;
            json["files"] = result.files;
            json["manifest"] = result.manifestPath;
            return json;
        }

    setError(error, "unknown analysis command: " + command);
    return nullptr;
}

} // namespace vwave
