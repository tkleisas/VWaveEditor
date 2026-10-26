#include "vwave/Session.h"

#include "vwave/Analysis.h"
#include "vwave/Capture.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Mastering.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Processing.h"
#include "vwave/Recipe.h"
#include "vwave/Spectral.h"
#include "vwave/Synth.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cctype>
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

bool prepareSelection(Timeline& timeline, const nlohmann::json& params,
                      std::string* error) {
    if (timeline.empty()) {
        setError(error, "no active track");
        return false;
    }
    const int index = timeline.activeTrack();
    Document& document = timeline.track(index).document;
    Range range;
    bool hasRange = false;
    if (!parseOptionalRange(document.audio(), params, range, hasRange, error))
        return false;
    if (hasRange) {
        document.setSelection(range.start, range.end);
        const auto& selection = document.selection();
        if (selection.active) {
            timeline.setSelection(
                selection.start + timeline.track(index).startFrame,
                selection.end + timeline.track(index).startFrame);
        }
    }
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

bool parseDoubleArray(const nlohmann::json& value,
                      std::vector<double>& out) {
    if (!value.is_array()) return false;
    out.clear();
    out.reserve(value.size());
    for (const auto& item : value) {
        if (!item.is_number()) return false;
        const double number = item.get<double>();
        if (!std::isfinite(number)) return false;
        out.push_back(number);
    }
    return true;
}

nlohmann::json wavetableToJson(const Wavetable& table) {
    const auto& options = table.designOptions();
    return {{"ok", true},
            {"name", table.name()},
            {"frame_size", table.frameSize()},
            {"frame_count", table.frameCount()},
            {"sample_rate", table.sampleRate()},
            {"root_note", options.rootNote},
            {"morph_mode", wavetableMorphModeName(options.morphMode)},
            {"interpolation",
             wavetableInterpolationName(options.interpolation)},
            {"oversampling", options.oversampling},
            {"keyframe_positions", table.framePositions()},
            {"band_limited", options.bandLimited},
            {"max_harmonic", options.maxHarmonic},
            {"quantization_bits", options.quantizationBits},
            {"normalized", options.normalized},
            {"dc_removed", options.dcRemoved},
            {"phase_aligned", options.phaseAligned}};
}

bool applyWavetableParams(Wavetable& table, const nlohmann::json& params,
                          std::string* error) {
    if (params.contains("root_note")) {
        table.setRootNote(params["root_note"].get<double>());
    }
    if (params.contains("morph_mode")) {
        if (!params["morph_mode"].is_string()) {
            setError(error, "morph_mode must be a string");
            return false;
        }
        WavetableMorphMode mode;
        const std::string name = params["morph_mode"].get<std::string>();
        if (!wavetableMorphModeFromName(name, mode)) {
            setError(error, "unknown morph_mode: " + name);
            return false;
        }
        table.setMorphMode(mode);
    }
    if (params.contains("interpolation")) {
        if (!params["interpolation"].is_string()) {
            setError(error, "interpolation must be a string");
            return false;
        }
        WavetableInterpolation interpolation;
        const std::string name = params["interpolation"].get<std::string>();
        if (!wavetableInterpolationFromName(name, interpolation)) {
            setError(error, "unknown interpolation: " + name);
            return false;
        }
        table.setInterpolation(interpolation);
    }
    if (params.contains("oversampling"))
        table.setOversampling(params.value("oversampling", 1));
    if (params.contains("keyframe_positions")) {
        std::vector<double> positions;
        if (!parseDoubleArray(params["keyframe_positions"], positions) ||
            !table.setFramePositions(positions)) {
            setError(error, "keyframe_positions must match frame_count");
            return false;
        }
    }
    if (params.value("align_frames", false)) table.phaseAlignFrames();
    if (params.contains("max_harmonic"))
        table.bandLimitFrames(params.value("max_harmonic", 0));
    else if (params.value("band_limit", false))
        table.bandLimitFrames(std::min(table.frameSize() / 2, 64));
    if (params.contains("quantize_bits"))
        table.quantizeFrames(params.value("quantize_bits", 0));
    if (params.value("normalize_frames", false)) table.normalizeFrames();
    if (params.value("remove_dc_frames", false)) table.removeDc();
    if (params.contains("random_phase_seed")) {
        const int seed = params.value("random_phase_seed", 0);
        table.randomizePhases(static_cast<uint32_t>(seed));
    }
    return true;
}

bool saveWavetableFile(const Wavetable& table, const std::string& path,
                       int sampleRate, std::string* error) {
    std::string extension =
        std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    if (extension == ".wav" || extension == ".wave")
        return table.saveWav(path, sampleRate, error);
    return table.save(path, error);
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

bool parseMasteringOptions(const nlohmann::json& params,
                           MasteringOptions& options, std::string* error) {
    if (params.contains("profile")) {
        if (!params["profile"].is_string()) {
            setError(error, "profile must be a string");
            return false;
        }
        const std::string name = params["profile"].get<std::string>();
        if (!masteringProfileFromName(name, options.profile)) {
            setError(error, "unknown mastering profile: " + name);
            return false;
        }
    }
    options.fftSize = optionalInt(params, "fft_size", options.fftSize);
    options.hopSize = optionalInt(params, "hop_size", options.hopSize);
    options.bandCount = optionalInt(params, "band_count", options.bandCount);
    options.minHz = optionalDouble(params, "min_hz", options.minHz);
    options.maxHz = optionalDouble(params, "max_hz", options.maxHz);
    options.maxCutDb = optionalDouble(params, "max_cut_db", options.maxCutDb);
    options.maxBoostDb =
        optionalDouble(params, "max_boost_db", options.maxBoostDb);
    options.smoothingBands = optionalInt(
        params, "smoothing_bands", options.smoothingBands);
    options.includeMuted =
        optionalBool(params, "include_muted", options.includeMuted);
    options.customStrength = optionalDouble(
        params, "custom_strength", options.customStrength);
    options.customMaxCutDb = optionalDouble(
        params, "custom_max_cut_db", options.customMaxCutDb);
    options.customMaxBoostDb = optionalDouble(
        params, "custom_max_boost_db", options.customMaxBoostDb);
    return true;
}

nlohmann::json okResult() { return {{"ok", true}}; }

} // namespace

Session::Session(Document document) {
    if (!document.source().empty()) {
        std::string name = "Track 1";
        if (!document.sourcePath().empty())
            name = std::filesystem::path(document.sourcePath())
                       .stem()
                       .string();
        m_timeline.addTrack(name, std::move(document), 0, nullptr);
    }
}

Document& Session::document() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return m_emptyDocument;
    return m_timeline.track(index).document;
}

const Document& Session::document() const {
    const int index = m_timeline.activeTrack();
    if (index < 0) return m_emptyDocument;
    return m_timeline.track(index).document;
}

void Session::syncSelectionFromActive() {
    if (m_timeline.empty()) return;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const auto& selection = track.document.selection();
    if (selection.active && selection.end > selection.start) {
        m_timeline.setSelection(selection.start + track.startFrame,
                                selection.end + track.startFrame);
    } else {
        m_timeline.clearSelection();
    }
}

bool Session::loadAudio(const std::string& path, std::string* error) {
    if (error) error->clear();
    Audio audio;
    if (!readAudio(path, audio, error)) return false;
    if (audio.empty()) {
        setError(error, "audio file is empty");
        return false;
    }
    Timeline timeline;
    std::string name = std::filesystem::path(path).stem().string();
    if (name.empty()) name = "Track 1";
    if (!timeline.addAudioTrack(name, audio, 0, error)) return false;
    timeline.track(0).document.setSourcePath(path);
    m_timeline = std::move(timeline);
    return true;
}

bool Session::saveAudio(const std::string& path,
                        const AudioExportOptions& options,
                        std::string* error) {
    if (m_timeline.empty()) {
        setError(error, "no active track");
        return false;
    }
    return document().saveAudio(path, options, error);
}

bool Session::saveMixdown(const std::string& path,
                          const AudioExportOptions& options,
                          std::string* error) {
    if (error) error->clear();
    const Audio mix = m_timeline.mixdown();
    if (mix.empty()) {
        setError(error, "project is empty");
        return false;
    }
    return writeAudio(path, mix, options, error);
}

bool Session::loadProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    return m_timeline.loadProject(path, error);
}

bool Session::saveProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    syncSelectionFromActive();
    return m_timeline.saveProject(path, error);
}

nlohmann::json Session::tracksInfo() const {
    nlohmann::json tracks = nlohmann::json::array();
    for (int i = 0; i < m_timeline.trackCount(); ++i) {
        const auto& track = m_timeline.track(i);
        const int rate = m_timeline.sampleRate();
        const int64_t frames = track.document.numFrames();
        tracks.push_back(
            {{"index", i},
             {"name", track.name},
             {"active", i == m_timeline.activeTrack()},
             {"start_frame", track.startFrame},
             {"start_seconds",
              rate > 0 ? static_cast<double>(track.startFrame) / rate : 0.0},
             {"frames", frames},
             {"duration",
              rate > 0 ? static_cast<double>(frames) / rate : 0.0},
             {"sample_rate", track.document.sampleRate()},
             {"channels", track.document.numChannels()},
             {"gain_db", track.gainDb},
             {"pan", track.pan},
             {"muted", track.muted},
             {"solo", track.solo},
             {"armed", track.armed},
             {"empty", track.empty},
             {"source_path", track.document.sourcePath()}});
    }
    return tracks;
}

nlohmann::json Session::info() {
    syncSelectionFromActive();
    nlohmann::json result;
    const Document& doc = document();
    const bool hasTrack = !m_timeline.empty();
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
    result["track_count"] = m_timeline.trackCount();
    result["active_track"] = m_timeline.activeTrack();
    result["project_sample_rate"] = m_timeline.sampleRate();
    result["project_frames"] = m_timeline.lengthFrames();
    result["project_duration"] = m_timeline.duration();
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
    result["timeline_selection"] = {
        {"start", m_timeline.selection().start},
        {"end", m_timeline.selection().end},
        {"active", m_timeline.selection().active},
        {"start_seconds",
         m_timeline.sampleRate() > 0
             ? static_cast<double>(m_timeline.selection().start) /
                   m_timeline.sampleRate()
             : 0.0},
        {"end_seconds",
         m_timeline.sampleRate() > 0
             ? static_cast<double>(m_timeline.selection().end) /
                   m_timeline.sampleRate()
             : 0.0}};
    result["markers"] = nlohmann::json::array();
    for (const auto& marker : doc.markers())
        result["markers"].push_back(markerToJson(marker, doc.sampleRate()));
    result["slices"] = nlohmann::json::array();
    for (const auto& slice : doc.slices())
        result["slices"].push_back(sliceToJson(slice, doc.sampleRate()));
    result["tracks"] = tracksInfo();
    result["active_track_name"] =
        hasTrack ? m_timeline.track(m_timeline.activeTrack()).name
                 : std::string{};
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

        if (command == "tracks" || command == "list_tracks" ||
            command == "track_info") {
            return {{"ok", true},
                    {"track_count", m_timeline.trackCount()},
                    {"tracks", tracksInfo()},
                    {"active_track", m_timeline.activeTrack()}};
        }
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

        if (command == "add_track") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            Audio audio;
            if (!readAudio(path, audio, error)) return nullptr;
            const double start = optionalDouble(params, "start", 0.0);
            if (!std::isfinite(start) || start < 0.0) {
                setError(error, "invalid track start time");
                return nullptr;
            }
            const int rate = m_timeline.empty() ? audio.sampleRate()
                                                : m_timeline.sampleRate();
            const int64_t startFrame =
                static_cast<int64_t>(std::llround(start * rate));
            std::string name = params.value("name", std::string{});
            if (name.empty()) {
                name = std::filesystem::path(path).stem().string();
                if (name.empty()) name = "Track " +
                                          std::to_string(
                                              m_timeline.trackCount() + 1);
            }
            if (!m_timeline.addAudioTrack(name, audio, startFrame, error))
                return nullptr;
            const int index = m_timeline.trackCount() - 1;
            m_timeline.setActiveTrack(index);
            m_timeline.track(index).document.setSourcePath(path);
            return info();
        }
        if (command == "add_empty_track") {
            const double start = optionalDouble(params, "start", 0.0);
            if (!std::isfinite(start) || start < 0.0) {
                setError(error, "invalid track start time");
                return nullptr;
            }
            const int rate = m_timeline.empty() ? 48000
                                                : m_timeline.sampleRate();
            const int64_t startFrame =
                static_cast<int64_t>(std::llround(start * rate));
            std::string name = params.value("name", std::string{});
            if (name.empty())
                name = "Track " +
                       std::to_string(m_timeline.trackCount() + 1);
            if (!m_timeline.addEmptyTrack(name, startFrame, error))
                return nullptr;
            m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
            return info();
        }
        if (command == "remove_track") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (!m_timeline.removeTrack(index)) {
                setError(error, "invalid track index");
                return nullptr;
            }
            return info();
        }
        if (command == "select_track") {
            if (!params.contains("index") || !params["index"].is_number()) {
                setError(error, "select_track requires an index");
                return nullptr;
            }
            const int index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            m_timeline.setActiveTrack(index);
            return info();
        }
        if (command == "track_set") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            if (params.contains("name"))
                m_timeline.setTrackName(index,
                                        params["name"].get<std::string>());
            if (params.contains("gain_db"))
                m_timeline.setTrackGainDb(index,
                                          params["gain_db"].get<double>());
            if (params.contains("pan"))
                m_timeline.setTrackPan(index, params["pan"].get<double>());
            if (params.contains("muted"))
                m_timeline.setTrackMuted(index,
                                         params["muted"].get<bool>());
            if (params.contains("solo"))
                m_timeline.setTrackSolo(index, params["solo"].get<bool>());
            if (params.contains("armed"))
                m_timeline.setTrackArmed(index, params["armed"].get<bool>());
            if (params.contains("start")) {
                const double start = params["start"].get<double>();
                if (!std::isfinite(start) || start < 0.0 ||
                    m_timeline.sampleRate() <= 0) {
                    setError(error, "invalid track start time");
                    return nullptr;
                }
                m_timeline.setTrackStartFrame(
                    index, static_cast<int64_t>(
                               std::llround(start * m_timeline.sampleRate())));
            }
            return info();
        }
        if (command == "save_mixdown") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            const AudioExportOptions options = exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!saveMixdown(path, options, error)) return nullptr;
            return {{"ok", true}, {"path", path}};
        }

        if (command == "load_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!loadAudio(path, error)) return nullptr;
            return info();
        }
        if (command == "save_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            const AudioExportOptions options = exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (optionalBool(params, "mixdown", false)) {
                if (!saveMixdown(path, options, error)) return nullptr;
            } else {
                if (!document().saveAudio(path, options, error)) return nullptr;
            }
            return {{"ok", true}, {"path", path}};
        }
        if (command == "load_project") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!loadProject(path, error)) return nullptr;
            return info();
        }
        if (command == "save_project") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!saveProject(path, error)) return nullptr;
            return {{"ok", true}, {"path", path}};
        }

        if (command == "select") {
            if (!params.contains("start") || !params.contains("end")) {
                setError(error, "select requires start and end");
                return nullptr;
            }
            Range range;
            bool hasRange = false;
            if (!parseOptionalRange(document().audio(), params, range,
                                    hasRange, error))
                return nullptr;
            if (!hasRange || range.empty()) {
                setError(error, "selection is empty");
                return nullptr;
            }
            document().setSelection(range.start, range.end);
            syncSelectionFromActive();
            return {{"ok", true}, {"selection", info()["selection"]}};
        }
        if (command == "select_all") {
            document().selectAll();
            syncSelectionFromActive();
            return {{"ok", true}, {"selection", info()["selection"]}};
        }
        if (command == "clear_selection") {
            document().clearSelection();
            m_timeline.clearSelection();
            return okResult();
        }

        if (command == "undo") {
            if (!document().undo()) {
                setError(error, "nothing to undo");
                return nullptr;
            }
            return info();
        }
        if (command == "redo") {
            if (!document().redo()) {
                setError(error, "nothing to redo");
                return nullptr;
            }
            return info();
        }

        if (command == "copy") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().copySelection()) {
                setError(error, "cannot copy selection");
                return nullptr;
            }
            return {{"ok", true},
                    {"clipboard_frames", document().clipboard().numFrames()}};
        }
        if (command == "cut") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().cutSelection(error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "delete") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().deleteSelection(error)) return nullptr;
            syncSelectionFromActive();
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
                    std::llround(at * document().sampleRate()));
                if (!document().pasteAt(frame, error)) return nullptr;
            } else {
                if (!document().paste(error)) return nullptr;
            }
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "crop") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().cropToSelection(error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

        if (command == "gain" || command == "normalize" ||
            command == "fade_in" || command == "fade_out" ||
            command == "reverse" || command == "invert" ||
            command == "silence" || command == "dc_remove") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            bool success = false;
            if (command == "gain")
                success = document().gainDb(optionalDouble(params, "db", 0.0),
                                            error);
            else if (command == "normalize")
                success = document().normalize(
                    optionalDouble(params, "peak_db", -1.0), error);
            else if (command == "fade_in")
                success = document().fadeIn(error);
            else if (command == "fade_out")
                success = document().fadeOut(error);
            else if (command == "reverse")
                success = document().reverseSelection(error);
            else if (command == "invert")
                success = document().invertSelection(error);
            else if (command == "silence")
                success = document().silenceSelection(error);
            else
                success = document().removeDc(error);
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
                std::llround(seconds * document().sampleRate()));
            const int64_t position = static_cast<int64_t>(
                std::llround(at * document().sampleRate()));
            Audio silence(document().numChannels(), frames,
                          document().sampleRate());
            if (!document().insertAudio(position, std::move(silence), error))
                return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "insert_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            Audio audio;
            if (!readAudio(path, audio, error)) return nullptr;
            if (audio.numChannels() != document().numChannels()) {
                setError(error, "inserted audio channel count does not match");
                return nullptr;
            }
            const double at = optionalDouble(params, "at", 0.0);
            if (!std::isfinite(at) || at < 0.0) {
                setError(error, "invalid insert position");
                return nullptr;
            }
            const int64_t position = static_cast<int64_t>(
                std::llround(at * document().sampleRate()));
            if (!document().insertAudio(position, std::move(audio), error))
                return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

        if (command == "time_stretch" || command == "paulstretch" ||
            command == "pitch_shift") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (command == "time_stretch") {
                const double ratio = params.value("ratio", 1.0);
                const StretchAlgorithm algorithm =
                    stretchAlgorithm(params, error);
                if (error && !error->empty()) return nullptr;
                if (!document().timeStretch(ratio, algorithm, error))
                    return nullptr;
            } else if (command == "paulstretch") {
                const double ratio = params.value("ratio", 1.0);
                if (!document().timeStretch(ratio, StretchAlgorithm::PaulStretch,
                                            error))
                    return nullptr;
            } else {
                const double semitones = params.value("semitones", 0.0);
                const StretchAlgorithm algorithm =
                    stretchAlgorithm(params, error);
                if (error && !error->empty()) return nullptr;
                if (!document().pitchShift(semitones, algorithm, error))
                    return nullptr;
            }
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "resample") {
            const int sampleRate = params.value("sample_rate", 0);
            if (!document().resample(sampleRate, error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

        if (command == "spectral_gain" || command == "spectral_filter" ||
            command == "spectral_gate" || command == "spectral_blur") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            bool success = false;
            if (command == "spectral_gain") {
                success = document().spectralGain(
                    optionalDouble(params, "low_hz", 0.0),
                    optionalDouble(params, "high_hz", 0.0),
                    optionalDouble(params, "db", 0.0), error);
            } else if (command == "spectral_filter") {
                success = document().spectralFilter(
                    params.value("type", std::string{}),
                    optionalDouble(params, "cutoff_hz", 0.0), error);
            } else if (command == "spectral_gate") {
                success = document().spectralGate(
                    optionalDouble(params, "threshold_db", -80.0), error);
            } else {
                success = document().spectralBlur(
                    optionalInt(params, "time_radius", 1),
                    optionalInt(params, "frequency_radius", 2), error);
            }
            if (!success) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

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

        if (command == "add_marker") {
            const double time = params.value("time", -1.0);
            if (!std::isfinite(time) || time < 0.0 ||
                time > document().duration()) {
                setError(error, "invalid marker time");
                return nullptr;
            }
            const int64_t frame = static_cast<int64_t>(
                std::llround(time * document().sampleRate()));
            if (!document().addMarker(
                    frame, params.value("name", std::string{}))) {
                setError(error, "cannot add marker");
                return nullptr;
            }
            return okResult();
        }
        if (command == "remove_marker") {
            const int index = params.value("index", -1);
            if (index < 0 ||
                !document().removeMarker(static_cast<size_t>(index))) {
                setError(error, "invalid marker index");
                return nullptr;
            }
            return okResult();
        }
        if (command == "clear_markers") {
            document().clearMarkers();
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
        "info", "tracks", "list_tracks", "track_info", "mastering_analysis",
        "image_spectrogram", "oscilloscope_render", "mastering_render",
        "regenerate_track", "add_track",
        "add_empty_track", "remove_track", "select_track", "track_set",
        "save_mixdown", "load_audio", "save_audio", "load_project",
        "save_project", "select", "select_all", "clear_selection", "undo",
        "redo",
        "copy", "cut", "delete", "paste", "crop", "gain", "normalize",
        "fade_in", "fade_out", "reverse", "invert", "silence", "dc_remove",
        "insert_silence", "insert_audio", "time_stretch", "paulstretch",
        "pitch_shift", "resample", "spectral_gain", "spectral_filter",
        "spectral_gate", "spectral_blur", "analyze_beats", "detect_pitch",
        "slices", "export_slices", "wavetable", "harmonics_wavetable",
        "wavetable_design", "synth_note", "generate_sweep", "deconvolve",
        "add_marker",
        "remove_marker", "clear_markers"};
}

} // namespace vwave
