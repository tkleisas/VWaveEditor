#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Capture.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Mastering.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Processing.h"
#include "vwave/Recipe.h"
#include "vwave/Synth.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeGeneratorCommands(const std::string& command,
                                                 const nlohmann::json& params,
                                                 std::string* error) {
        if (command == "oscilloscope_render") {
            const std::string out = requiredString(params, "out", error);
            if (out.empty()) return nullptr;
            ScopePath path;
            std::string shapeName = "polyline";
            if (params.contains("path_file")) {
                const std::string file = requiredString(
                    params, "path_file", error);
                if (file.empty() ||
                    !loadScopePath(file, path, error))
                    return nullptr;
            } else if (params.contains("points") &&
                       params["points"].is_array()) {
                if (params["points"].size() < 2) {
                    setError(error, "scope points must be an array");
                    return nullptr;
                }
                path.name = params.value("name", std::string{"Path"});
                path.closed = optionalBool(params, "closed", false);
                for (const auto& point : params["points"]) {
                    if (!point.is_array() || point.size() != 2 ||
                        !point[0].is_number() || !point[1].is_number()) {
                        setError(error, "scope points must be [x, y] pairs");
                        return nullptr;
                    }
                    const double x = point[0].get<double>();
                    const double y = point[1].get<double>();
                    if (!std::isfinite(x) || !std::isfinite(y)) {
                        setError(error, "scope point is not finite");
                        return nullptr;
                    }
                    path.points.push_back({x, y});
                }
            } else {
                ScopeShape shape = ScopeShape::Circle;
                shapeName = params.value("shape", std::string{"circle"});
                if (!scopeShapeFromName(shapeName, shape)) {
                    setError(error, "unknown scope shape: " + shapeName);
                    return nullptr;
                }
                path = makeScopeShape(
                    shape, optionalInt(params, "point_count", 256),
                    optionalDouble(params, "size", 0.8),
                    optionalDouble(params, "a", 3.0),
                    optionalDouble(params, "b", 2.0),
                    optionalDouble(params, "phase", 0.0),
                    optionalInt(params, "turns", 4),
                    params.value("name", std::string{}));
            }
            if (!path.valid()) {
                setError(error, "scope path is empty");
                return nullptr;
            }
            ScopeRenderOptions options;
            options.sampleRate = optionalInt(
                params, "sample_rate",
                m_timeline.sampleRate() > 0 ? m_timeline.sampleRate()
                                            : 48000);
            options.duration = optionalDouble(params, "duration", 2.0);
            options.scanHz = optionalDouble(params, "scan_hz", 120.0);
            options.gain = optionalDouble(params, "gain", 0.8);
            options.smoothing = optionalDouble(params, "smoothing", 0.0);
            options.bandLimited =
                optionalBool(params, "band_limited", true);
            options.bandLimitHz =
                optionalDouble(params, "band_limit_hz", 16000.0);
            options.oversampling = optionalInt(params, "oversampling", 4);
            options.normalize = optionalBool(params, "normalize", true);
            options.peak = optionalDouble(params, "peak", 0.9);
            if (params.contains("direction")) {
                if (!params["direction"].is_string()) {
                    setError(error, "direction must be a string");
                    return nullptr;
                }
                const std::string directionName =
                    params["direction"].get<std::string>();
                if (!scopeDirectionFromName(directionName,
                                            options.direction)) {
                    setError(error,
                             "unknown scope direction: " + directionName);
                    return nullptr;
                }
            }
            const Audio rendered = renderScopePath(path, options, error);
            if (rendered.empty()) return nullptr;
            AudioExportOptions exportOptions =
                exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!writeAudio(out, rendered, exportOptions, error))
                return nullptr;
            if (params.contains("save_path")) {
                const std::string savePath = requiredString(
                    params, "save_path", error);
                if (savePath.empty() ||
                    !saveScopePath(path, savePath, error))
                    return nullptr;
            }
            if (optionalBool(params, "replace", false)) {
                int trackIndex = -1;
                if (m_timeline.empty()) {
                    if (!m_timeline.addAudioTrack(
                            path.name.empty() ? "Scope" : path.name,
                            rendered, 0, error))
                        return nullptr;
                    trackIndex = 0;
                } else {
                    const int index = m_timeline.activeTrack();
                    if (!m_timeline.setTrackAudio(
                            index, rendered,
                            m_timeline.track(index).startFrame, error))
                        return nullptr;
                    trackIndex = index;
                }
                if (trackIndex >= 0)
                    m_timeline.track(trackIndex).recipe =
                        makeScopeTrackRecipe(path, options);
            }
            return {{"ok", true},
                    {"path", out},
                    {"name", path.name},
                    {"shape", shapeName},
                    {"points", path.points.size()},
                    {"closed", path.closed},
                    {"sample_rate", rendered.sampleRate()},
                    {"channels", rendered.numChannels()},
                    {"frames", rendered.numFrames()},
                    {"duration", rendered.duration()},
                    {"scan_hz", options.scanHz},
                    {"band_limited", options.bandLimited},
                    {"oversampling", options.oversampling},
                    {"direction",
                     scopeDirectionName(options.direction)}};
        }

        if (command == "image_spectrogram") {
            const std::string imagePath = requiredString(params, "path", error);
            if (imagePath.empty()) return nullptr;
            const std::string out = requiredString(params, "out", error);
            if (out.empty()) return nullptr;
            ImageData image;
            if (!loadImageFile(imagePath, image, error)) return nullptr;
            ImageSpectrogramOptions options;
            options.sampleRate = optionalInt(
                params, "sample_rate",
                m_timeline.sampleRate() > 0 ? m_timeline.sampleRate()
                                            : 48000);
            options.fftSize = optionalInt(params, "fft_size", 2048);
            options.hopSize = optionalInt(params, "hop_size", 512);
            options.duration = optionalDouble(params, "duration", 0.0);
            options.minHz = optionalDouble(params, "min_hz", 20.0);
            options.maxHz = optionalDouble(params, "max_hz", 20000.0);
            options.loops = optionalInt(params, "loops", 1);
            options.cropLeft = optionalDouble(params, "crop_left", 0.0);
            options.cropRight = optionalDouble(params, "crop_right", 1.0);
            options.cropTop = optionalDouble(params, "crop_top", 0.0);
            options.cropBottom =
                optionalDouble(params, "crop_bottom", 1.0);
            options.flipHorizontal =
                optionalBool(params, "flip_horizontal", false);
            options.flipVertical =
                optionalBool(params, "flip_vertical", false);
            options.invert = optionalBool(params, "invert", false);
            options.gamma = optionalDouble(params, "gamma", 1.0);
            options.contrast = optionalDouble(params, "contrast", 1.0);
            options.threshold =
                optionalDouble(params, "threshold", 0.0);
            options.useAlpha = optionalBool(params, "use_alpha", true);
            options.iterations = optionalInt(params, "iterations", 24);
            options.momentum = optionalDouble(params, "momentum", 0.99);
            options.seed = static_cast<uint32_t>(
                optionalInt(params, "seed", 1));
            options.channels = optionalInt(params, "channels", 1);
            options.normalize = optionalBool(params, "normalize", true);
            options.peak = optionalDouble(params, "peak", 0.9);
            if (params.contains("frequency_scale")) {
                if (!params["frequency_scale"].is_string()) {
                    setError(error, "frequency_scale must be a string");
                    return nullptr;
                }
                const std::string name =
                    params["frequency_scale"].get<std::string>();
                if (!imageFrequencyScaleFromName(name,
                                                 options.frequencyScale)) {
                    setError(error,
                             "unknown image frequency scale: " + name);
                    return nullptr;
                }
            }
            if (params.contains("time_mapping")) {
                if (!params["time_mapping"].is_string()) {
                    setError(error, "time_mapping must be a string");
                    return nullptr;
                }
                const std::string name =
                    params["time_mapping"].get<std::string>();
                if (!imageTimeMappingFromName(name, options.timeMapping)) {
                    setError(error, "unknown image time mapping: " + name);
                    return nullptr;
                }
            }
            const std::string phaseName =
                params.value("phase_mode", std::string{"fast_griffin_lim"});
            if (!imagePhaseModeFromName(phaseName, options.phaseMode)) {
                setError(error, "unknown image phase mode: " + phaseName);
                return nullptr;
            }
            const ImageSpectrogramResult generated =
                imageToSpectrogram(image, options, error);
            if (generated.audio.empty()) return nullptr;
            AudioExportOptions exportOptions =
                exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!writeAudio(out, generated.audio, exportOptions, error))
                return nullptr;
            if (optionalBool(params, "replace", false)) {
                int trackIndex = -1;
                if (m_timeline.empty()) {
                    if (!m_timeline.addAudioTrack("Image", generated.audio, 0,
                                                  error))
                        return nullptr;
                    trackIndex = 0;
                } else {
                    const int index = m_timeline.activeTrack();
                    if (!m_timeline.setTrackAudio(
                            index, generated.audio,
                            m_timeline.track(index).startFrame, error))
                        return nullptr;
                    trackIndex = index;
                }
                if (trackIndex >= 0)
                    m_timeline.track(trackIndex).recipe =
                        makeImageTrackRecipe(imagePath, options);
            }
            return {{"ok", true},
                    {"path", out},
                    {"image_width", generated.imageWidth},
                    {"image_height", generated.imageHeight},
                    {"sample_rate", generated.audio.sampleRate()},
                    {"channels", generated.audio.numChannels()},
                    {"frames", generated.audio.numFrames()},
                    {"duration", generated.duration},
                    {"fft_size", generated.fftSize},
                    {"hop_size", generated.hopSize},
                    {"frame_count", generated.frameCount},
                    {"bin_count", generated.binCount},
                    {"phase_mode", imagePhaseModeName(options.phaseMode)},
                    {"frequency_scale",
                     imageFrequencyScaleName(options.frequencyScale)},
                    {"time_mapping",
                     imageTimeMappingName(options.timeMapping)}};
        }

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

        if (command == "regenerate_track") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            Audio regenerated;
            if (!regenerateTrack(m_timeline, index, regenerated, error))
                return nullptr;
            const int64_t start = m_timeline.track(index).startFrame;
            if (!m_timeline.setTrackAudio(index, std::move(regenerated),
                                          start, error))
                return nullptr;
            m_timeline.setActiveTrack(index);
            return {{"ok", true},
                    {"index", index},
                    {"recipe_type",
                     m_timeline.track(index).recipe.value(
                         "type", std::string{})}};
        }

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

    setError(error, "unknown generator command: " + command);
    return nullptr;
}

} // namespace vwave
