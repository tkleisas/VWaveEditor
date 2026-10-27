#pragma once

#include "Base64.h"
#include "vwave/ClipArrangement.h"
#include "vwave/Document.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace vwave::detail {

inline void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

inline Selection clampSelection(Selection selection, int64_t frames) {
    if (!selection.active) return {};
    const int64_t start = std::clamp<int64_t>(selection.start, 0, frames);
    const int64_t end = std::clamp<int64_t>(selection.end, 0, frames);
    if (end <= start) return {};
    return {start, end, true};
}

inline Selection selectionFromJson(const nlohmann::json& json) {
    Selection selection;
    if (!json.is_object()) return selection;
    selection.start = json.value("start", int64_t{0});
    selection.end = json.value("end", int64_t{0});
    selection.active = json.value("active", false);
    return selection;
}

inline nlohmann::json selectionToJson(const Selection& selection) {
    return {{"start", selection.start},
            {"end", selection.end},
            {"active", selection.active}};
}

inline bool clipsFromOperation(const Operation& operation,
                               std::vector<Clip>& clips,
                               std::string* error) {
    clips.clear();
    if (!operation.params.contains("clips") ||
        !operation.params["clips"].is_array()) {
        setError(error, "clip operation is missing its clip list");
        return false;
    }
    for (const auto& value : operation.params["clips"]) {
        Clip clip;
        if (!clipFromJson(value, clip, error)) return false;
        clips.push_back(std::move(clip));
    }
    return true;
}

inline std::string operationKindName(Operation::Kind kind) {
    switch (kind) {
    case Operation::Kind::Process: return "process";
    case Operation::Kind::Delete: return "delete";
    case Operation::Kind::Insert: return "insert";
    case Operation::Kind::ReplaceRange: return "replace";
    case Operation::Kind::MoveRange: return "move_range";
    case Operation::Kind::RepeatRange: return "repeat_range";
    case Operation::Kind::SetClips: return "set_clips";
    case Operation::Kind::FlattenClips: return "flatten_clips";
    case Operation::Kind::Crop: return "crop";
    case Operation::Kind::Resample: return "resample";
    case Operation::Kind::ReplaceAll: return "replace_all";
    }
    return "process";
}

inline bool operationKindFromName(const std::string& name,
                                  Operation::Kind& kind) {
    if (name == "process") kind = Operation::Kind::Process;
    else if (name == "delete") kind = Operation::Kind::Delete;
    else if (name == "insert") kind = Operation::Kind::Insert;
    else if (name == "replace") kind = Operation::Kind::ReplaceRange;
    else if (name == "move_range") kind = Operation::Kind::MoveRange;
    else if (name == "repeat_range") kind = Operation::Kind::RepeatRange;
    else if (name == "set_clips") kind = Operation::Kind::SetClips;
    else if (name == "flatten_clips") kind = Operation::Kind::FlattenClips;
    else if (name == "crop") kind = Operation::Kind::Crop;
    else if (name == "resample") kind = Operation::Kind::Resample;
    else if (name == "replace_all") kind = Operation::Kind::ReplaceAll;
    else return false;
    return true;
}

inline std::string processName(Operation::Process process) {
    switch (process) {
    case Operation::Process::Gain: return "gain";
    case Operation::Process::Normalize: return "normalize";
    case Operation::Process::FadeIn: return "fade_in";
    case Operation::Process::FadeOut: return "fade_out";
    case Operation::Process::Reverse: return "reverse";
    case Operation::Process::Invert: return "invert";
    case Operation::Process::Silence: return "silence";
    case Operation::Process::DcRemove: return "dc_remove";
    case Operation::Process::TimeStretch: return "time_stretch";
    case Operation::Process::PitchShift: return "pitch_shift";
    case Operation::Process::SpectralGain: return "spectral_gain";
    case Operation::Process::SpectralFilter: return "spectral_filter";
    case Operation::Process::SpectralGate: return "spectral_gate";
    case Operation::Process::SpectralBlur: return "spectral_blur";
    case Operation::Process::Convolve: return "convolve";
    }
    return "gain";
}

inline bool processFromName(const std::string& name,
                            Operation::Process& process) {
    if (name == "gain") process = Operation::Process::Gain;
    else if (name == "normalize") process = Operation::Process::Normalize;
    else if (name == "fade_in") process = Operation::Process::FadeIn;
    else if (name == "fade_out") process = Operation::Process::FadeOut;
    else if (name == "reverse") process = Operation::Process::Reverse;
    else if (name == "invert") process = Operation::Process::Invert;
    else if (name == "silence") process = Operation::Process::Silence;
    else if (name == "dc_remove") process = Operation::Process::DcRemove;
    else if (name == "time_stretch") process = Operation::Process::TimeStretch;
    else if (name == "pitch_shift") process = Operation::Process::PitchShift;
    else if (name == "spectral_gain") process = Operation::Process::SpectralGain;
    else if (name == "spectral_filter")
        process = Operation::Process::SpectralFilter;
    else if (name == "spectral_gate")
        process = Operation::Process::SpectralGate;
    else if (name == "spectral_blur")
        process = Operation::Process::SpectralBlur;
    else if (name == "convolve") process = Operation::Process::Convolve;
    else return false;
    return true;
}

inline nlohmann::json audioToJson(const Audio& audio) {
    nlohmann::json json;
    json["sampleRate"] = audio.sampleRate();
    json["channels"] = audio.numChannels();
    json["frames"] = audio.numFrames();
    const auto* bytes = reinterpret_cast<const uint8_t*>(audio.data());
    json["data"] =
        base64Encode(bytes, audio.totalSamples() * sizeof(float));
    return json;
}

inline bool audioFromJson(const nlohmann::json& json, Audio& audio,
                          std::string* error) {
    if (!json.is_object()) {
        setError(error, "audio payload must be an object");
        return false;
    }
    const int sampleRate = json.value("sampleRate", 0);
    const int channels = json.value("channels", 0);
    const int64_t frames = json.value("frames", int64_t{0});
    if (sampleRate <= 0 || channels <= 0 || frames < 0) {
        setError(error, "invalid audio payload dimensions");
        return false;
    }
    if (!json.contains("data") || !json["data"].is_string()) {
        setError(error, "audio payload is missing data");
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!base64Decode(json["data"].get<std::string>(), bytes)) {
        setError(error, "audio payload is not valid base64");
        return false;
    }
    const size_t expected = static_cast<size_t>(channels) *
                            static_cast<size_t>(frames) * sizeof(float);
    if (bytes.size() != expected) {
        setError(error, "audio payload size does not match dimensions");
        return false;
    }
    Audio result(channels, frames, sampleRate);
    if (!bytes.empty())
        std::memcpy(result.data(), bytes.data(), bytes.size());
    audio = std::move(result);
    return true;
}

inline std::string joinLabel(const std::string& name, double value,
                             const std::string& suffix = {}) {
    std::ostringstream out;
    out << name;
    if (std::isfinite(value)) out << ' ' << value;
    if (!suffix.empty()) out << ' ' << suffix;
    return out.str();
}

} // namespace vwave::detail
