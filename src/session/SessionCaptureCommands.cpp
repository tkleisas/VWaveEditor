#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Capture.h"
#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeCaptureCommands(const std::string& command,
                                              const nlohmann::json& params,
                                              std::string* error) {
        if (command == "generate_sweep") {
            SweepOptions options;
            options.seconds = optionalDouble(params, "seconds", 5.0);
            options.startHz = optionalDouble(params, "start_hz", 20.0);
            options.endHz = optionalDouble(params, "end_hz", 20000.0);
            options.amplitude = optionalDouble(params, "amplitude", 0.5);
            options.sampleRate =
                optionalInt(params, "sample_rate", document().sampleRate());
            options.channels = optionalInt(params, "channels", 1);
            const Audio sweep = generateSweep(options);
            if (sweep.empty()) {
                setError(error, "cannot generate sweep");
                return nullptr;
            }
            if (params.contains("out")) {
                const std::string out = params["out"].get<std::string>();
                if (!writeAudio(out, sweep, {}, error)) return nullptr;
            }
            if (optionalBool(params, "replace", false)) {
                if (m_timeline.empty()) {
                    if (!m_timeline.addAudioTrack("Sweep", sweep, 0, error))
                        return nullptr;
                } else {
                    const int index = m_timeline.activeTrack();
                    if (!m_timeline.setTrackAudio(
                            index, sweep, m_timeline.track(index).startFrame,
                            error))
                        return nullptr;
                }
            }
            if (!params.contains("out") &&
                !optionalBool(params, "replace", false)) {
                setError(error, "generate_sweep requires out or replace");
                return nullptr;
            }
            return {{"ok", true},
                    {"sample_rate", sweep.sampleRate()},
                    {"channels", sweep.numChannels()},
                    {"frames", sweep.numFrames()}};
        }

        if (command == "deconvolve") {
            const std::string recordedPath =
                requiredString(params, "recorded", error);
            const std::string sweepPath =
                requiredString(params, "sweep", error);
            if (recordedPath.empty() || sweepPath.empty()) return nullptr;
            Audio recorded;
            Audio sweep;
            if (!readAudio(recordedPath, recorded, error)) return nullptr;
            if (!readAudio(sweepPath, sweep, error)) return nullptr;
            DeconvolutionOptions options;
            options.irLength =
                params.value("ir_length", int64_t{0});
            options.trimToPeak =
                optionalBool(params, "trim_to_peak", false);
            options.preDelaySeconds =
                optionalDouble(params, "pre_delay", 0.0);
            options.normalize = optionalBool(params, "normalize", true);
            const Audio ir = extractImpulseResponse(recorded, sweep, options,
                                                    error);
            if (ir.empty()) return nullptr;
            if (params.contains("out")) {
                const std::string out = params["out"].get<std::string>();
                AudioExportOptions exportOptions =
                    exportOptionsFrom(params, error);
                if (error && !error->empty()) return nullptr;
                if (!writeAudio(out, ir, exportOptions, error)) return nullptr;
            }
            if (params.contains("manifest")) {
                CaptureInfo info;
                info.name = params.value("name", std::string{"capture"});
                info.source = recordedPath;
                info.sampleRate = ir.sampleRate();
                info.frames = ir.numFrames();
                if (!writeCaptureManifest(
                        params["manifest"].get<std::string>(), info, error))
                    return nullptr;
            }
            return {{"ok", true},
                    {"sample_rate", ir.sampleRate()},
                    {"channels", ir.numChannels()},
                    {"frames", ir.numFrames()},
                    {"peak", ir.peak()}};
        }

    setError(error, "unknown command: " + command);
    return nullptr;
}

} // namespace vwave
