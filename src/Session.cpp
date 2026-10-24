#include "vwave/Session.h"

#include "vwave/Analysis.h"
#include "vwave/Capture.h"
#include "vwave/Processing.h"
#include "vwave/Spectral.h"
#include "vwave/Synth.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace vwave {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool parseFormat(const std::string& name, AudioFormat& format) {
    if (name == "auto") format = AudioFormat::Auto;
    else if (name == "wav") format = AudioFormat::Wav;
    else if (name == "aiff") format = AudioFormat::Aiff;
    else if (name == "flac") format = AudioFormat::Flac;
    else if (name == "ogg") format = AudioFormat::Ogg;
    else return false;
    return true;
}

bool parseBitDepth(const std::string& name, BitDepth& depth) {
    if (name == "int16") depth = BitDepth::Int16;
    else if (name == "int24") depth = BitDepth::Int24;
    else if (name == "float32" || name == "float") depth = BitDepth::Float32;
    else return false;
    return true;
}

AudioExportOptions exportOptionsFrom(const nlohmann::json& params,
                                     std::string* error) {
    AudioExportOptions options;
    const std::string format =
        params.value("format", std::string{"auto"});
    if (!parseFormat(format, options.format)) {
        setError(error, "unknown audio format: " + format);
        return options;
    }
    const std::string depth =
        params.value("bit_depth", std::string{"float32"});
    if (!parseBitDepth(depth, options.bitDepth)) {
        setError(error, "unknown bit depth: " + depth);
        return options;
    }
    return options;
}

bool parseOptionalRange(const Audio& audio, const nlohmann::json& params,
                        Range& range, bool& hasRange, std::string* error) {
    hasRange = false;
    const bool hasStart = params.contains("start");
    const bool hasEnd = params.contains("end");
    if (!hasStart && !hasEnd) return true;
    if (hasStart != hasEnd) {
        setError(error, "start and end must be provided together");
        return false;
    }
    const double start = params["start"].get<double>();
    const double end = params["end"].get<double>();
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 ||
        end <= start) {
        setError(error, "invalid time range");
        return false;
    }
    const int64_t startFrame = static_cast<int64_t>(
        std::llround(start * audio.sampleRate()));
    const int64_t endFrame = static_cast<int64_t>(
        std::llround(end * audio.sampleRate()));
    range = Range{startFrame, endFrame}.clamped(audio.numFrames());
    if (range.empty()) {
        setError(error, "time range is outside the document");
        return false;
    }
    hasRange = true;
    return true;
}

bool prepareSelection(Document& document, const nlohmann::json& params,
                      std::string* error) {
    Range range;
    bool hasRange = false;
    if (!parseOptionalRange(document.audio(), params, range, hasRange, error))
        return false;
    if (hasRange) document.setSelection(range.start, range.end);
    return true;
}

StretchAlgorithm stretchAlgorithm(const nlohmann::json& params,
                                  std::string* error) {
    const std::string name =
        params.value("algorithm", std::string{"wsola"});
    StretchAlgorithm algorithm = StretchAlgorithm::Wsola;
    if (!stretchAlgorithmFromName(name, algorithm)) {
        setError(error, "unknown stretch algorithm: " + name);
    }
    return algorithm;
}

nlohmann::json sliceToJson(const Slice& slice, int sampleRate) {
    return {{"name", slice.name},
            {"start", slice.range.start},
            {"end", slice.range.end},
            {"start_seconds",
             static_cast<double>(slice.range.start) / sampleRate},
            {"end_seconds",
             static_cast<double>(slice.range.end) / sampleRate}};
}

nlohmann::json markerToJson(const Marker& marker, int sampleRate) {
    return {{"frame", marker.frame},
            {"time", static_cast<double>(marker.frame) / sampleRate},
            {"name", marker.name}};
}

std::string requiredString(const nlohmann::json& params, const char* key,
                           std::string* error) {
    if (!params.contains(key) || !params[key].is_string()) {
        setError(error, std::string("missing string parameter: ") + key);
        return {};
    }
    return params[key].get<std::string>();
}

int optionalInt(const nlohmann::json& params, const char* key, int fallback) {
    if (!params.contains(key)) return fallback;
    return params[key].get<int>();
}

double optionalDouble(const nlohmann::json& params, const char* key,
                      double fallback) {
    if (!params.contains(key)) return fallback;
    return params[key].get<double>();
}

bool optionalBool(const nlohmann::json& params, const char* key, bool fallback) {
    if (!params.contains(key)) return fallback;
    return params[key].get<bool>();
}

nlohmann::json okResult() { return {{"ok", true}}; }

} // namespace

Session::Session(Document document) : m_document(std::move(document)) {}

bool Session::loadAudio(const std::string& path, std::string* error) {
    return m_document.loadAudio(path, error);
}

bool Session::saveAudio(const std::string& path,
                        const AudioExportOptions& options,
                        std::string* error) {
    return m_document.saveAudio(path, options, error);
}

bool Session::loadProject(const std::string& path, std::string* error) {
    return m_document.loadProject(path, error);
}

bool Session::saveProject(const std::string& path, std::string* error) {
    return m_document.saveProject(path, error);
}

nlohmann::json Session::info() const {
    nlohmann::json result;
    const Document& doc = m_document;
    result["ok"] = true;
    result["source_path"] = doc.sourcePath();
    result["sample_rate"] = doc.sampleRate();
    result["channels"] = doc.numChannels();
    result["frames"] = doc.numFrames();
    result["duration"] = doc.duration();
    result["source_frames"] = doc.source().numFrames();
    result["undo_count"] = doc.undoCount();
    result["redo_count"] = doc.redoCount();
    result["undo_label"] = doc.undoLabel();
    result["redo_label"] = doc.redoLabel();
    result["clipboard_frames"] = doc.clipboard().numFrames();
    result["selection"] = {
        {"start", doc.selection().start},
        {"end", doc.selection().end},
        {"active", doc.selection().active},
        {"start_seconds", doc.sampleRate() > 0
                              ? static_cast<double>(doc.selection().start) /
                                    doc.sampleRate()
                              : 0.0},
        {"end_seconds", doc.sampleRate() > 0
                            ? static_cast<double>(doc.selection().end) /
                                  doc.sampleRate()
                            : 0.0}};
    result["markers"] = nlohmann::json::array();
    for (const auto& marker : doc.markers())
        result["markers"].push_back(markerToJson(marker, doc.sampleRate()));
    result["slices"] = nlohmann::json::array();
    for (const auto& slice : doc.slices())
        result["slices"].push_back(sliceToJson(slice, doc.sampleRate()));
    return result;
}

nlohmann::json Session::execute(const std::string& command,
                                const nlohmann::json& parameters,
                                std::string* error) {
    if (error) error->clear();
    try {
        if (!parameters.is_object() && !parameters.is_null()) {
            setError(error, "parameters must be an object");
            return nullptr;
        }
        const nlohmann::json params =
            parameters.is_null() ? nlohmann::json::object() : parameters;

        if (command == "info") return info();

        if (command == "load_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!m_document.loadAudio(path, error)) return nullptr;
            return info();
        }
        if (command == "save_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            const AudioExportOptions options = exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!m_document.saveAudio(path, options, error)) return nullptr;
            return {{"ok", true}, {"path", path}};
        }
        if (command == "load_project") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!m_document.loadProject(path, error)) return nullptr;
            return info();
        }
        if (command == "save_project") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!m_document.saveProject(path, error)) return nullptr;
            return {{"ok", true}, {"path", path}};
        }

        if (command == "select") {
            if (!params.contains("start") || !params.contains("end")) {
                setError(error, "select requires start and end");
                return nullptr;
            }
            Range range;
            bool hasRange = false;
            if (!parseOptionalRange(m_document.audio(), params, range,
                                    hasRange, error))
                return nullptr;
            if (!hasRange || range.empty()) {
                setError(error, "selection is empty");
                return nullptr;
            }
            m_document.setSelection(range.start, range.end);
            return {{"ok", true}, {"selection", info()["selection"]}};
        }
        if (command == "select_all") {
            m_document.selectAll();
            return {{"ok", true}, {"selection", info()["selection"]}};
        }
        if (command == "clear_selection") {
            m_document.clearSelection();
            return okResult();
        }

        if (command == "undo") {
            if (!m_document.undo()) {
                setError(error, "nothing to undo");
                return nullptr;
            }
            return info();
        }
        if (command == "redo") {
            if (!m_document.redo()) {
                setError(error, "nothing to redo");
                return nullptr;
            }
            return info();
        }

        if (command == "copy") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            if (!m_document.copySelection()) {
                setError(error, "cannot copy selection");
                return nullptr;
            }
            return {{"ok", true},
                    {"clipboard_frames", m_document.clipboard().numFrames()}};
        }
        if (command == "cut") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            if (!m_document.cutSelection(error)) return nullptr;
            return okResult();
        }
        if (command == "delete") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            if (!m_document.deleteSelection(error)) return nullptr;
            return okResult();
        }
        if (command == "paste") {
            if (params.contains("at")) {
                const double at = params["at"].get<double>();
                if (!std::isfinite(at) || at < 0.0) {
                    setError(error, "invalid paste position");
                    return nullptr;
                }
                const int64_t frame = static_cast<int64_t>(
                    std::llround(at * m_document.sampleRate()));
                if (!m_document.pasteAt(frame, error)) return nullptr;
            } else {
                if (!m_document.paste(error)) return nullptr;
            }
            return okResult();
        }
        if (command == "crop") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            if (!m_document.cropToSelection(error)) return nullptr;
            return okResult();
        }

        if (command == "gain" || command == "normalize" ||
            command == "fade_in" || command == "fade_out" ||
            command == "reverse" || command == "invert" ||
            command == "silence" || command == "dc_remove") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            bool success = false;
            if (command == "gain")
                success = m_document.gainDb(optionalDouble(params, "db", 0.0),
                                            error);
            else if (command == "normalize")
                success = m_document.normalize(
                    optionalDouble(params, "peak_db", -1.0), error);
            else if (command == "fade_in")
                success = m_document.fadeIn(error);
            else if (command == "fade_out")
                success = m_document.fadeOut(error);
            else if (command == "reverse")
                success = m_document.reverseSelection(error);
            else if (command == "invert")
                success = m_document.invertSelection(error);
            else if (command == "silence")
                success = m_document.silenceSelection(error);
            else
                success = m_document.removeDc(error);
            if (!success) return nullptr;
            return okResult();
        }

        if (command == "insert_silence") {
            const double seconds = params.value("seconds", 0.0);
            const double at = params.value("at", 0.0);
            if (!std::isfinite(seconds) || seconds <= 0.0 ||
                !std::isfinite(at) || at < 0.0) {
                setError(error, "invalid silence parameters");
                return nullptr;
            }
            const int64_t frames = static_cast<int64_t>(
                std::llround(seconds * m_document.sampleRate()));
            const int64_t position = static_cast<int64_t>(
                std::llround(at * m_document.sampleRate()));
            Audio silence(m_document.numChannels(), frames,
                          m_document.sampleRate());
            if (!m_document.insertAudio(position, std::move(silence), error))
                return nullptr;
            return okResult();
        }
        if (command == "insert_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            Audio audio;
            if (!readAudio(path, audio, error)) return nullptr;
            if (audio.numChannels() != m_document.numChannels()) {
                setError(error, "inserted audio channel count does not match");
                return nullptr;
            }
            const double at = optionalDouble(params, "at", 0.0);
            if (!std::isfinite(at) || at < 0.0) {
                setError(error, "invalid insert position");
                return nullptr;
            }
            const int64_t position = static_cast<int64_t>(
                std::llround(at * m_document.sampleRate()));
            if (!m_document.insertAudio(position, std::move(audio), error))
                return nullptr;
            return okResult();
        }

        if (command == "time_stretch" || command == "paulstretch" ||
            command == "pitch_shift") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            if (command == "time_stretch") {
                const double ratio = params.value("ratio", 1.0);
                const StretchAlgorithm algorithm =
                    stretchAlgorithm(params, error);
                if (error && !error->empty()) return nullptr;
                if (!m_document.timeStretch(ratio, algorithm, error))
                    return nullptr;
            } else if (command == "paulstretch") {
                const double ratio = params.value("ratio", 1.0);
                if (!m_document.timeStretch(ratio, StretchAlgorithm::PaulStretch,
                                            error))
                    return nullptr;
            } else {
                const double semitones = params.value("semitones", 0.0);
                const StretchAlgorithm algorithm =
                    stretchAlgorithm(params, error);
                if (error && !error->empty()) return nullptr;
                if (!m_document.pitchShift(semitones, algorithm, error))
                    return nullptr;
            }
            return okResult();
        }
        if (command == "resample") {
            const int sampleRate = params.value("sample_rate", 0);
            if (!m_document.resample(sampleRate, error)) return nullptr;
            return okResult();
        }

        if (command == "spectral_gain" || command == "spectral_filter" ||
            command == "spectral_gate" || command == "spectral_blur") {
            if (!prepareSelection(m_document, params, error)) return nullptr;
            bool success = false;
            if (command == "spectral_gain") {
                success = m_document.spectralGain(
                    optionalDouble(params, "low_hz", 0.0),
                    optionalDouble(params, "high_hz", 0.0),
                    optionalDouble(params, "db", 0.0), error);
            } else if (command == "spectral_filter") {
                success = m_document.spectralFilter(
                    params.value("type", std::string{}),
                    optionalDouble(params, "cutoff_hz", 0.0), error);
            } else if (command == "spectral_gate") {
                success = m_document.spectralGate(
                    optionalDouble(params, "threshold_db", -80.0), error);
            } else {
                success = m_document.spectralBlur(
                    optionalInt(params, "time_radius", 1),
                    optionalInt(params, "frequency_radius", 2), error);
            }
            if (!success) return nullptr;
            return okResult();
        }

        if (command == "analyze_beats") {
            const BeatAnalysis analysis = analyzeBeats(
                m_document.audio(), optionalDouble(params, "sensitivity", 0.5));
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
                                  m_document.sampleRate()},
                     {"strength", onset.strength}});
            result["beats"] = nlohmann::json::array();
            for (int64_t beat : analysis.beats) {
                result["beats"].push_back(
                    {{"frame", beat},
                     {"time", static_cast<double>(beat) /
                                  m_document.sampleRate()}});
            }
            return result;
        }
        if (command == "detect_pitch") {
            Range range;
            bool hasRange = false;
            if (!parseOptionalRange(m_document.audio(), params, range,
                                    hasRange, error))
                return nullptr;
            PitchOptions options;
            options.minHz = optionalDouble(params, "min_hz", 40.0);
            options.maxHz = optionalDouble(params, "max_hz", 2000.0);
            const PitchEstimate estimate =
                detectPitch(m_document.audio(), hasRange ? range : Range{},
                            options);
            return {{"ok", true},
                    {"frequency", estimate.frequency},
                    {"confidence", estimate.confidence},
                    {"frame", estimate.frame},
                    {"time", m_document.sampleRate() > 0
                                 ? static_cast<double>(estimate.frame) /
                                       m_document.sampleRate()
                                 : 0.0}};
        }

        if (command == "slices") {
            const std::string mode =
                params.value("mode", std::string{"transients"});
            std::vector<Slice> slices;
            if (mode == "transients") {
                slices = sliceByTransients(
                    m_document.audio(),
                    optionalDouble(params, "sensitivity", 0.5),
                    optionalDouble(params, "min_slice_seconds", 0.03));
            } else if (mode == "beats") {
                double bpm = optionalDouble(params, "bpm", 0.0);
                if (bpm <= 0.0)
                    bpm = analyzeBeats(m_document.audio()).bpm;
                slices = sliceByBeats(
                    m_document.audio(), bpm,
                    optionalInt(params, "beats_per_slice", 1));
            } else if (mode == "markers") {
                std::vector<int64_t> markers;
                for (const Marker& marker : m_document.markers())
                    markers.push_back(marker.frame);
                slices = sliceByMarkers(m_document.audio(), markers);
            } else {
                setError(error, "unknown slice mode: " + mode);
                return nullptr;
            }
            if (slices.empty()) {
                setError(error, "no slices were produced");
                return nullptr;
            }
            m_document.setSlices(slices);
            nlohmann::json result;
            result["ok"] = true;
            result["slices"] = nlohmann::json::array();
            for (const Slice& slice : slices)
                result["slices"].push_back(
                    sliceToJson(slice, m_document.sampleRate()));
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
            if (!exportSlices(m_document.audio(), m_document.slices(),
                              directory, options, &result, error))
                return nullptr;
            nlohmann::json json;
            json["ok"] = true;
            json["files"] = result.files;
            json["manifest"] = result.manifestPath;
            return json;
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
                if (!parseOptionalRange(m_document.audio(), params, range,
                                        hasRange, error))
                    return nullptr;
                if (hasRange) {
                    options.range = range;
                } else if (m_document.selection().active) {
                    options.range = m_document.selectedRange();
                }
                table = extractWavetable(m_document.audio(), options, error);
                if (!table.valid()) return nullptr;
            } else {
                if (!params.contains("amplitudes") ||
                    !params["amplitudes"].is_array()) {
                    setError(error, "missing amplitudes array");
                    return nullptr;
                }
                std::vector<double> amplitudes;
                for (const auto& value : params["amplitudes"])
                    amplitudes.push_back(value.get<double>());
                table = Wavetable::fromHarmonics(
                    amplitudes, optionalInt(params, "frame_size", 2048),
                    params.value("name", std::string{"Wavetable"}));
                table.setSampleRate(m_document.sampleRate());
                if (!table.valid()) {
                    setError(error, "cannot create wavetable from harmonics");
                    return nullptr;
                }
            }

            const std::string extension =
                std::filesystem::path(path).extension().string();
            if (extension == ".wav" || extension == ".wave") {
                if (!table.saveWav(path,
                                   optionalInt(params, "sample_rate",
                                               m_document.sampleRate()),
                                   error))
                    return nullptr;
            } else {
                if (!table.save(path, error)) return nullptr;
            }
            return {{"ok", true},
                    {"path", path},
                    {"name", table.name()},
                    {"frame_size", table.frameSize()},
                    {"frame_count", table.frameCount()},
                    {"sample_rate", table.sampleRate()}};
        }

        if (command == "synth_note") {
            const std::string path =
                requiredString(params, "wavetable", error);
            if (path.empty()) return nullptr;
            Wavetable table;
            const std::string extension =
                std::filesystem::path(path).extension().string();
            if (extension == ".wav" || extension == ".wave") {
                if (!Wavetable::loadWav(
                        path, optionalInt(params, "frame_size", 2048), table,
                        error))
                    return nullptr;
            } else {
                if (!Wavetable::load(path, table, error)) return nullptr;
            }
            SynthNoteOptions options;
            options.sampleRate = optionalInt(params, "sample_rate", 0);
            options.channels = optionalInt(params, "channels", 1);
            options.rootNote = optionalDouble(params, "root_note", 69.0);
            options.morph = optionalDouble(params, "morph", 0.0);
            options.gain = optionalDouble(params, "gain", 0.8);
            options.unison = optionalInt(params, "unison", 1);
            options.detuneCents =
                optionalDouble(params, "detune_cents", 0.0);
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
            if (optionalBool(params, "replace", false))
                m_document = Document::fromAudio(note);
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
                optionalInt(params, "sample_rate", m_document.sampleRate());
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
            if (optionalBool(params, "replace", false))
                m_document = Document::fromAudio(sweep);
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

        if (command == "add_marker") {
            const double time = params.value("time", -1.0);
            if (!std::isfinite(time) || time < 0.0 ||
                time > m_document.duration()) {
                setError(error, "invalid marker time");
                return nullptr;
            }
            const int64_t frame = static_cast<int64_t>(
                std::llround(time * m_document.sampleRate()));
            if (!m_document.addMarker(
                    frame, params.value("name", std::string{}))) {
                setError(error, "cannot add marker");
                return nullptr;
            }
            return okResult();
        }
        if (command == "remove_marker") {
            const int index = params.value("index", -1);
            if (index < 0 ||
                !m_document.removeMarker(static_cast<size_t>(index))) {
                setError(error, "invalid marker index");
                return nullptr;
            }
            return okResult();
        }
        if (command == "clear_markers") {
            m_document.clearMarkers();
            return okResult();
        }

        setError(error, "unknown command: " + command);
        return nullptr;
    } catch (const std::exception& e) {
        setError(error, std::string("command failed: ") + e.what());
        return nullptr;
    }
}

std::vector<std::string> Session::commandNames() {
    return {
        "info", "load_audio", "save_audio", "load_project", "save_project",
        "select", "select_all", "clear_selection", "undo", "redo",
        "copy", "cut", "delete", "paste", "crop", "gain", "normalize",
        "fade_in", "fade_out", "reverse", "invert", "silence", "dc_remove",
        "insert_silence", "insert_audio", "time_stretch", "paulstretch",
        "pitch_shift", "resample", "spectral_gain", "spectral_filter",
        "spectral_gate", "spectral_blur", "analyze_beats", "detect_pitch",
        "slices", "export_slices", "wavetable", "harmonics_wavetable",
        "synth_note", "generate_sweep", "deconvolve", "add_marker",
        "remove_marker", "clear_markers"};
}

} // namespace vwave
