#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Recipe.h"
#include "vwave/Synth.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeWavetableCommands(const std::string& command,
                                                const nlohmann::json& params,
                                                std::string* error) {
        if (command == "wavetable" || command == "harmonics_wavetable") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            Wavetable table;
            if (command == "wavetable") {
                WavetableExtractOptions options;
                options.frameSize =
                    optionalInt(params, "frame_size", 2048);
                options.frameCount =
                    optionalInt(params, "frame_count", 8);
                options.autoCycle =
                    optionalBool(params, "auto_cycle", true);
                options.cycleLength =
                    optionalInt(params, "cycle_length", 0);
                options.normalizeFrames =
                    optionalBool(params, "normalize", true);
                options.removeDc =
                    optionalBool(params, "remove_dc", true);
                options.phaseAlign =
                    optionalBool(params, "phase_align", true);
                options.name =
                    params.value("name", std::string{"Wavetable"});
                Range range;
                bool hasRange = false;
                if (!parseOptionalRange(document().audio(), params, range,
                                        hasRange, error))
                    return nullptr;
                if (hasRange) {
                    options.range = range;
                } else if (document().selection().active) {
                    options.range = document().selectedRange();
                }
                table = extractWavetable(document().audio(), options, error);
                if (!table.valid()) return nullptr;
            } else {
                if (!params.contains("amplitudes") ||
                    !params["amplitudes"].is_array()) {
                    setError(error, "missing amplitudes array");
                    return nullptr;
                }
                std::vector<double> amplitudes;
                if (!parseDoubleArray(params["amplitudes"], amplitudes) ||
                    amplitudes.size() < 2) {
                    setError(error, "amplitudes must be a numeric array");
                    return nullptr;
                }
                std::vector<double> phases;
                if (params.contains("phases") &&
                    !parseDoubleArray(params["phases"], phases)) {
                    setError(error, "phases must be a numeric array");
                    return nullptr;
                }
                table = Wavetable::fromHarmonics(
                    amplitudes, phases,
                    optionalInt(params, "frame_size", 2048),
                    params.value("name", std::string{"Wavetable"}),
                    optionalBool(params, "normalize", true));
                if (!table.valid()) {
                    setError(error, "cannot create wavetable from harmonics");
                    return nullptr;
                }
            }

            if (!applyWavetableParams(table, params, error)) return nullptr;
            const int sampleRate =
                optionalInt(params, "sample_rate",
                            table.sampleRate() > 0 ? table.sampleRate()
                                                   : document().sampleRate());
            if (sampleRate > 0) table.setSampleRate(sampleRate);
            if (!saveWavetableFile(table, path,
                                   sampleRate > 0 ? sampleRate : 48000,
                                   error))
                return nullptr;
            nlohmann::json result = wavetableToJson(table);
            result["path"] = path;
            return result;
        }

        if (command == "wavetable_design") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!params.contains("frames") || !params["frames"].is_array() ||
                params["frames"].empty()) {
                setError(error, "frames must be a non-empty array");
                return nullptr;
            }
            const int frameSize =
                std::clamp(optionalInt(params, "frame_size", 2048), 64, 65536);
            const std::string name =
                params.value("name", std::string{"Wavetable"});
            const int sampleRate =
                optionalInt(params, "sample_rate",
                            document().sampleRate() > 0
                                ? document().sampleRate()
                                : 48000);
            Wavetable table(frameSize, name);
            table.setSampleRate(sampleRate > 0 ? sampleRate : 48000);
            const auto& frames = params["frames"];
            bool anyPositions = false;
            for (const auto& frame : frames) {
                if (frame.contains("position")) {
                    anyPositions = true;
                    break;
                }
            }
            for (size_t frameIndex = 0; frameIndex < frames.size();
                 ++frameIndex) {
                const auto& frame = frames[frameIndex];
                if (!frame.is_object()) {
                    setError(error, "each frame must be an object");
                    return nullptr;
                }
                std::vector<float> samples;
                if (frame.contains("samples")) {
                    std::vector<double> values;
                    if (!parseDoubleArray(frame["samples"], values) ||
                        static_cast<int>(values.size()) != frameSize) {
                        setError(error, "frame samples must match frame_size");
                        return nullptr;
                    }
                    samples.resize(values.size());
                    for (size_t i = 0; i < values.size(); ++i)
                        samples[i] = static_cast<float>(values[i]);
                } else if (frame.contains("harmonics")) {
                    std::vector<double> amplitudes;
                    if (!parseDoubleArray(frame["harmonics"], amplitudes) ||
                        amplitudes.size() < 2) {
                        setError(error, "frame harmonics must be numeric");
                        return nullptr;
                    }
                    std::vector<double> phases;
                    if (frame.contains("phases") &&
                        !parseDoubleArray(frame["phases"], phases)) {
                        setError(error, "frame phases must be numeric");
                        return nullptr;
                    }
                    Wavetable temporary(frameSize);
                    temporary.addFrame(
                        std::vector<float>(static_cast<size_t>(frameSize),
                                           0.0f));
                    if (!temporary.setFrameFromHarmonics(
                            0, amplitudes, phases,
                            frame.value("normalize", false))) {
                        setError(error, "cannot build frame from harmonics");
                        return nullptr;
                    }
                    samples = temporary.frame(0);
                } else {
                    setError(error,
                             "frame requires samples or harmonics array");
                    return nullptr;
                }
                const double defaultPosition =
                    frames.size() > 1
                        ? static_cast<double>(frameIndex) /
                              static_cast<double>(frames.size() - 1)
                        : 0.0;
                const double position =
                    frame.value("position", defaultPosition);
                if (anyPositions) {
                    if (!table.addKeyframe(samples, position)) {
                        setError(error, "cannot add keyframe");
                        return nullptr;
                    }
                } else {
                    if (!table.addFrame(samples)) {
                        setError(error, "cannot add frame");
                        return nullptr;
                    }
                }
            }
            if (!applyWavetableParams(table, params, error)) return nullptr;
            if (!saveWavetableFile(table, path, sampleRate, error))
                return nullptr;
            nlohmann::json result = wavetableToJson(table);
            result["path"] = path;
            return result;
        }

        if (command == "synth_note") {
            const std::string path =
                requiredString(params, "wavetable", error);
            if (path.empty()) return nullptr;
            Wavetable table;
            if (!Wavetable::loadAuto(path, table, error)) return nullptr;
            SynthNoteOptions options;
            options.sampleRate = optionalInt(params, "sample_rate", 0);
            options.channels = optionalInt(params, "channels", 1);
            options.rootNote = optionalDouble(params, "root_note", 69.0);
            options.useWavetableRootNote =
                optionalBool(params, "use_table_root", false);
            options.morph = optionalDouble(params, "morph", 0.0);
            options.gain = optionalDouble(params, "gain", 0.8);
            options.unison = optionalInt(params, "unison", 1);
            options.detuneCents =
                optionalDouble(params, "detune_cents", 0.0);
            options.oversampling = optionalInt(params, "oversampling", 0);
            if (params.contains("blend_weights")) {
                if (!parseDoubleArray(params["blend_weights"],
                                      options.blendWeights)) {
                    setError(error, "blend_weights must be a numeric array");
                    return nullptr;
                }
            }
            options.envelope.attack =
                optionalDouble(params, "attack", 0.005);
            options.envelope.decay =
                optionalDouble(params, "decay", 0.12);
            options.envelope.sustain =
                optionalDouble(params, "sustain", 0.75);
            options.envelope.release =
                optionalDouble(params, "release", 0.25);
            const Audio note = renderWavetableNote(
                table, optionalDouble(params, "midi_note", 60.0),
                optionalDouble(params, "duration", 1.0), options);
            if (note.empty()) {
                setError(error, "cannot render synth note");
                return nullptr;
            }
            if (params.contains("out")) {
                const std::string out = params["out"].get<std::string>();
                AudioExportOptions exportOptions =
                    exportOptionsFrom(params, error);
                if (error && !error->empty()) return nullptr;
                if (!writeAudio(out, note, exportOptions, error))
                    return nullptr;
            }
            if (optionalBool(params, "replace", false)) {
                int trackIndex = -1;
                if (m_timeline.empty()) {
                    if (!m_timeline.addAudioTrack("Synth", note, 0, error))
                        return nullptr;
                    trackIndex = 0;
                } else {
                    const int index = m_timeline.activeTrack();
                    if (!m_timeline.setTrackAudio(
                            index, note, m_timeline.track(index).startFrame,
                            error))
                        return nullptr;
                    trackIndex = index;
                }
                if (trackIndex >= 0)
                    m_timeline.track(trackIndex).recipe =
                        makeSynthTrackRecipe(
                            path, optionalDouble(params, "midi_note", 60.0),
                            optionalDouble(params, "duration", 1.0), options);
            }
            return {{"ok", true},
                    {"sample_rate", note.sampleRate()},
                    {"channels", note.numChannels()},
                    {"frames", note.numFrames()},
                    {"duration", note.duration()}};
        }

    setError(error, "unknown command: " + command);
    return nullptr;
}

} // namespace vwave
