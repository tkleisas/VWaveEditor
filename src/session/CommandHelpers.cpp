#include "CommandHelpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace vwave::session {

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

} // namespace vwave::session
