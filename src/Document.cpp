#include "vwave/Document.h"

#include "Base64.h"
#include "vwave/Processing.h"
#include "vwave/Spectral.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

Selection clampSelection(Selection selection, int64_t frames) {
    if (!selection.active) return {};
    const int64_t start = std::clamp<int64_t>(selection.start, 0, frames);
    const int64_t end = std::clamp<int64_t>(selection.end, 0, frames);
    if (end <= start) return {};
    return {start, end, true};
}

Selection selectionFromJson(const nlohmann::json& json) {
    Selection selection;
    if (!json.is_object()) return selection;
    selection.start = json.value("start", int64_t{0});
    selection.end = json.value("end", int64_t{0});
    selection.active = json.value("active", false);
    return selection;
}

nlohmann::json selectionToJson(const Selection& selection) {
    return {{"start", selection.start},
            {"end", selection.end},
            {"active", selection.active}};
}

bool clipsFromOperation(const Operation& operation,
                        std::vector<Clip>& clips, std::string* error) {
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

std::string operationKindName(Operation::Kind kind) {
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

bool operationKindFromName(const std::string& name, Operation::Kind& kind) {
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

std::string processName(Operation::Process process) {
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

bool processFromName(const std::string& name, Operation::Process& process) {
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
    else if (name == "spectral_filter") process = Operation::Process::SpectralFilter;
    else if (name == "spectral_gate") process = Operation::Process::SpectralGate;
    else if (name == "spectral_blur") process = Operation::Process::SpectralBlur;
    else if (name == "convolve") process = Operation::Process::Convolve;
    else return false;
    return true;
}

nlohmann::json audioToJson(const Audio& audio) {
    nlohmann::json json;
    json["sampleRate"] = audio.sampleRate();
    json["channels"] = audio.numChannels();
    json["frames"] = audio.numFrames();
    const auto* bytes = reinterpret_cast<const uint8_t*>(audio.data());
    json["data"] = base64Encode(bytes, audio.totalSamples() * sizeof(float));
    return json;
}

bool audioFromJson(const nlohmann::json& json, Audio& audio,
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
    if (!bytes.empty()) std::memcpy(result.data(), bytes.data(), bytes.size());
    audio = std::move(result);
    return true;
}

std::string joinLabel(const std::string& name, double value,
                      const std::string& suffix = {}) {
    std::ostringstream out;
    out << name;
    if (std::isfinite(value)) out << ' ' << value;
    if (!suffix.empty()) out << ' ' << suffix;
    return out.str();
}

} // namespace

const char* stretchAlgorithmName(StretchAlgorithm algorithm) {
    switch (algorithm) {
    case StretchAlgorithm::Wsola: return "wsola";
    case StretchAlgorithm::PhaseVocoder: return "phase_vocoder";
    case StretchAlgorithm::PaulStretch: return "paulstretch";
    }
    return "wsola";
}

bool stretchAlgorithmFromName(const std::string& name,
                              StretchAlgorithm& algorithm) {
    if (name == "wsola") algorithm = StretchAlgorithm::Wsola;
    else if (name == "phase_vocoder" || name == "phase-vocoder")
        algorithm = StretchAlgorithm::PhaseVocoder;
    else if (name == "paulstretch" || name == "paul_stretch")
        algorithm = StretchAlgorithm::PaulStretch;
    else return false;
    return true;
}

Operation Operation::gain(double db, Range range) {
    Operation operation;
    operation.process = Process::Gain;
    operation.range = range;
    operation.params = {{"db", db}};
    operation.label = joinLabel("Gain", db, "dB");
    return operation;
}

Operation Operation::normalize(double peakDb, Range range) {
    Operation operation;
    operation.process = Process::Normalize;
    operation.range = range;
    operation.params = {{"peakDb", peakDb}};
    operation.label = joinLabel("Normalize", peakDb, "dB");
    return operation;
}

Operation Operation::fadeIn(Range range) {
    Operation operation;
    operation.process = Process::FadeIn;
    operation.range = range;
    operation.label = "Fade in";
    return operation;
}

Operation Operation::fadeOut(Range range) {
    Operation operation;
    operation.process = Process::FadeOut;
    operation.range = range;
    operation.label = "Fade out";
    return operation;
}

Operation Operation::reverse(Range range) {
    Operation operation;
    operation.process = Process::Reverse;
    operation.range = range;
    operation.label = "Reverse";
    return operation;
}

Operation Operation::invert(Range range) {
    Operation operation;
    operation.process = Process::Invert;
    operation.range = range;
    operation.label = "Invert";
    return operation;
}

Operation Operation::silence(Range range) {
    Operation operation;
    operation.process = Process::Silence;
    operation.range = range;
    operation.label = "Silence";
    return operation;
}

Operation Operation::dcRemove(Range range) {
    Operation operation;
    operation.process = Process::DcRemove;
    operation.range = range;
    operation.label = "Remove DC";
    return operation;
}

Operation Operation::timeStretch(double ratio, Range range,
                                 StretchAlgorithm algorithm) {
    Operation operation;
    operation.process = Process::TimeStretch;
    operation.range = range;
    operation.params = {{"ratio", ratio},
                        {"algorithm", stretchAlgorithmName(algorithm)}};
    operation.label = joinLabel("Time stretch", ratio, "x");
    return operation;
}

Operation Operation::pitchShift(double semitones, Range range,
                                StretchAlgorithm algorithm) {
    Operation operation;
    operation.process = Process::PitchShift;
    operation.range = range;
    operation.params = {{"semitones", semitones},
                        {"algorithm", stretchAlgorithmName(algorithm)}};
    operation.label = joinLabel("Pitch shift", semitones, "st");
    return operation;
}

Operation Operation::spectralGain(double lowHz, double highHz, double gainDb,
                                  Range range) {
    Operation operation;
    operation.process = Process::SpectralGain;
    operation.range = range;
    operation.params = {{"lowHz", lowHz},
                        {"highHz", highHz},
                        {"db", gainDb}};
    operation.label = joinLabel("Spectral gain", gainDb, "dB");
    return operation;
}

Operation Operation::spectralFilter(const std::string& type, double cutoffHz,
                                    Range range) {
    Operation operation;
    operation.process = Process::SpectralFilter;
    operation.range = range;
    operation.params = {{"type", type}, {"cutoffHz", cutoffHz}};
    operation.label = "Spectral " + type + " filter";
    return operation;
}

Operation Operation::spectralGate(double thresholdDb, Range range) {
    Operation operation;
    operation.process = Process::SpectralGate;
    operation.range = range;
    operation.params = {{"thresholdDb", thresholdDb}};
    operation.label = joinLabel("Spectral gate", thresholdDb, "dB");
    return operation;
}

Operation Operation::spectralBlur(int timeRadius, int frequencyRadius,
                                  Range range) {
    Operation operation;
    operation.process = Process::SpectralBlur;
    operation.range = range;
    operation.params = {{"timeRadius", timeRadius},
                        {"frequencyRadius", frequencyRadius}};
    operation.label = "Spectral blur";
    return operation;
}

Operation Operation::convolve(Audio impulseResponse, Range range,
                              bool normalizeIr) {
    Operation operation;
    operation.process = Process::Convolve;
    operation.range = range;
    operation.payload = std::move(impulseResponse);
    operation.params = {{"normalizeIr", normalizeIr}};
    operation.label = "Convolution";
    return operation;
}

Operation Operation::remove(Range range) {
    Operation operation;
    operation.kind = Kind::Delete;
    operation.range = range;
    operation.label = "Delete";
    return operation;
}

Operation Operation::insert(int64_t frame, Audio audio) {
    Operation operation;
    operation.kind = Kind::Insert;
    operation.range = {frame, frame};
    operation.payload = std::move(audio);
    operation.label = "Insert audio";
    return operation;
}

Operation Operation::replaceRange(Range range, Audio audio) {
    Operation operation;
    operation.kind = Kind::ReplaceRange;
    operation.range = range;
    operation.payload = std::move(audio);
    operation.label = "Replace audio";
    return operation;
}

Operation Operation::moveRange(Range range, int64_t destination, bool copy) {
    Operation operation;
    operation.kind = Kind::MoveRange;
    operation.range = range;
    operation.params = {{"destination", destination}, {"copy", copy}};
    operation.label = copy ? "Copy range" : "Move range";
    return operation;
}

Operation Operation::repeatRange(Range range, int repeats) {
    Operation operation;
    operation.kind = Kind::RepeatRange;
    operation.range = range;
    operation.params = {{"repeats", repeats}};
    operation.label = "Repeat range";
    return operation;
}

Operation Operation::setClips(std::vector<Clip> clips) {
    Operation operation;
    operation.kind = Kind::SetClips;
    operation.params["clips"] = nlohmann::json::array();
    for (const auto& clip : clips)
        operation.params["clips"].push_back(clipToJson(clip));
    operation.label = "Edit clips";
    return operation;
}

Operation Operation::flattenClips() {
    Operation operation;
    operation.kind = Kind::FlattenClips;
    operation.label = "Flatten clips";
    return operation;
}

Operation Operation::crop(Range range) {
    Operation operation;
    operation.kind = Kind::Crop;
    operation.range = range;
    operation.label = "Crop";
    return operation;
}

Operation Operation::resample(int sampleRate) {
    Operation operation;
    operation.kind = Kind::Resample;
    operation.sampleRate = sampleRate;
    operation.label = "Resample";
    return operation;
}

Operation Operation::replaceAll(Audio audio) {
    Operation operation;
    operation.kind = Kind::ReplaceAll;
    operation.payload = std::move(audio);
    operation.label = "Replace document";
    return operation;
}

nlohmann::json Operation::toJson() const {
    nlohmann::json json;
    json["kind"] = operationKindName(kind);
    json["process"] = processName(process);
    json["start"] = range.start;
    json["end"] = range.end;
    json["sampleRate"] = sampleRate;
    json["params"] = params;
    json["label"] = label;
    if (!payload.empty()) json["payload"] = audioToJson(payload);
    return json;
}

bool Operation::fromJson(const nlohmann::json& json, Operation& out,
                         std::string* error) {
    try {
        if (!json.is_object()) {
            setError(error, "operation must be an object");
            return false;
        }
        Operation operation;
        if (!operationKindFromName(json.value("kind", std::string{}),
                                   operation.kind)) {
            setError(error, "unknown operation kind");
            return false;
        }
        if (!processFromName(json.value("process", std::string{"gain"}),
                             operation.process)) {
            setError(error, "unknown process");
            return false;
        }
        operation.range.start = json.value("start", int64_t{0});
        operation.range.end = json.value("end", int64_t{0});
        operation.sampleRate = json.value("sampleRate", 0);
        operation.params = json.value("params", nlohmann::json::object());
        operation.label = json.value("label", std::string{});
        if (json.contains("payload")) {
            if (!audioFromJson(json["payload"], operation.payload, error))
                return false;
        }
        out = std::move(operation);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot parse operation: ") + e.what());
        return false;
    }
}

Document::Document() = default;

Document::Document(Audio source) : m_source(std::move(source)) {}

Document Document::fromAudio(Audio source) {
    return Document(std::move(source));
}

const Audio& Document::audio() const {
    if (!m_cacheValid) {
        const_cast<Document*>(this)->rebuild();
    }
    if (!m_arrangedValid) {
        const_cast<Document*>(this)->refreshArranged();
    }
    return m_arranged;
}

void Document::refreshArranged() {
    m_arranged = m_clips.render(m_rendered);
    m_arrangedValid = true;
}

int Document::sampleRate() const { return audio().sampleRate(); }
int Document::numChannels() const { return audio().numChannels(); }
int64_t Document::numFrames() const { return audio().numFrames(); }
double Document::duration() const { return audio().duration(); }

void Document::setSelection(int64_t start, int64_t end) {
    Selection selection;
    selection.start = start;
    selection.end = end;
    selection.active = end > start;
    setSelection(selection);
}

void Document::setSelection(const Selection& selection) {
    m_selection = clampSelection(selection, numFrames());
}

void Document::selectAll() {
    m_selection = {0, numFrames(), numFrames() > 0};
}

void Document::clearSelection() { m_selection.clear(); }

Range Document::selectedRange() const {
    if (!m_selection.active) return {};
    return m_selection.range();
}

Range Document::activeRange() const {
    if (m_selection.active) return m_selection.range();
    return {0, numFrames()};
}

bool Document::hasClips() const {
    if (!m_cacheValid) const_cast<Document*>(this)->rebuild();
    return !m_clips.empty();
}

const std::vector<Clip>& Document::clips() const {
    if (!m_cacheValid) const_cast<Document*>(this)->rebuild();
    return m_clips.clips();
}

int Document::clipIndexAt(int64_t frame) const {
    if (!m_cacheValid) const_cast<Document*>(this)->rebuild();
    return m_clips.indexAt(frame);
}

bool Document::createClipArrangement(std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    if (!m_clips.empty()) return true;
    std::vector<Clip> clips;
    if (!m_clips.makeFull(m_rendered.numFrames(), clips, error)) return false;
    return setClipArrangement(std::move(clips), "Create clips", error);
}

bool Document::setClipArrangement(std::vector<Clip> clips,
                                  std::string label, std::string* error) {
    if (clips.empty()) {
        setError(error, "clip arrangement is empty");
        return false;
    }
    Operation operation = Operation::setClips(std::move(clips));
    if (!label.empty()) operation.label = std::move(label);
    return apply(operation, error);
}

bool Document::splitClipAt(int64_t frame, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.splitAt(frame, m_rendered.numFrames(), clips, error))
        return false;
    return setClipArrangement(std::move(clips), "Split clip", error);
}

bool Document::deleteClip(size_t index, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.remove(index, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Delete clip", error);
}

bool Document::moveClip(size_t index, int64_t timelineStart,
                        std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.move(index, timelineStart, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Move clip", error);
}

bool Document::stretchClip(size_t index, double ratio,
                           std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.stretch(index, ratio, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Stretch clip", error);
}

bool Document::repeatClip(size_t index, int count, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.repeat(index, count, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Repeat clip", error);
}

bool Document::setClipGain(size_t index, double gainDb,
                           std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.setGain(index, gainDb, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Set clip gain", error);
}

bool Document::setClipFades(size_t index, double fadeInSeconds,
                            double fadeOutSeconds, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.setFades(index, fadeInSeconds, fadeOutSeconds, clips, error))
        return false;
    return setClipArrangement(std::move(clips), "Set clip fades", error);
}

bool Document::flattenClips(std::string* error) {
    return apply(Operation::flattenClips(), error);
}

std::vector<Operation> Document::operationHistory() const {
    std::vector<Operation> result;
    result.reserve(m_history.size());
    for (const auto& entry : m_history) result.push_back(entry.operation);
    return result;
}

bool Document::validateRange(const Operation& operation,
                             std::string* error) const {
    const int64_t frames = m_rendered.numFrames();
    auto validateSampleRange = [&](const Range& range, const char* name) {
        if (range.start < 0 || range.end > frames || range.length() <= 0) {
            setError(error, std::string(name) + " range is outside the document");
            return false;
        }
        return true;
    };

    switch (operation.kind) {
    case Operation::Kind::Process:
        if (!validateSampleRange(operation.range, "process")) return false;
        if (operation.process == Operation::Process::TimeStretch ||
            operation.process == Operation::Process::PitchShift) {
            const bool stretch = operation.process ==
                                 Operation::Process::TimeStretch;
            const char* key = stretch ? "ratio" : "semitones";
            if (!operation.params.contains(key) ||
                !operation.params[key].is_number()) {
                setError(error, std::string("missing numeric parameter: ") + key);
                return false;
            }
            const double value = operation.params[key].get<double>();
            if (!std::isfinite(value) || (stretch && value <= 0.0)) {
                setError(error, "invalid time/pitch parameter");
                return false;
            }
        }
        if (operation.process == Operation::Process::SpectralGain) {
            if (!operation.params.contains("lowHz") ||
                !operation.params.contains("highHz")) {
                setError(error, "spectral gain requires lowHz and highHz");
                return false;
            }
            const double low = operation.params["lowHz"].get<double>();
            const double high = operation.params["highHz"].get<double>();
            if (low < 0.0 || high <= low) {
                setError(error, "invalid spectral frequency range");
                return false;
            }
        }
        if (operation.process == Operation::Process::SpectralFilter) {
            const std::string type =
                operation.params.value("type", std::string{});
            const double cutoff =
                operation.params.value("cutoffHz", 0.0);
            if ((type != "lowpass" && type != "highpass") ||
                !std::isfinite(cutoff) || cutoff <= 0.0) {
                setError(error, "invalid spectral filter parameters");
                return false;
            }
        }
        if (operation.process == Operation::Process::SpectralGate) {
            const double threshold =
                operation.params.value("thresholdDb", 0.0);
            if (!std::isfinite(threshold)) {
                setError(error, "invalid spectral gate threshold");
                return false;
            }
        }
        if (operation.process == Operation::Process::SpectralBlur) {
            const int timeRadius =
                operation.params.value("timeRadius", 0);
            const int frequencyRadius =
                operation.params.value("frequencyRadius", 0);
            if (timeRadius < 0 || frequencyRadius < 0) {
                setError(error, "invalid spectral blur radius");
                return false;
            }
        }
        if (operation.process == Operation::Process::Convolve &&
            operation.payload.empty()) {
            setError(error, "convolution requires an impulse response");
            return false;
        }
        return true;
    case Operation::Kind::Delete:
        return validateSampleRange(operation.range, "delete");
    case Operation::Kind::ReplaceRange:
        if (!validateSampleRange(operation.range, "replace")) return false;
        if (operation.payload.empty()) {
            setError(error, "replacement audio is empty");
            return false;
        }
        if (operation.payload.numChannels() != m_rendered.numChannels()) {
            setError(error, "replacement channel count does not match");
            return false;
        }
        return true;
    case Operation::Kind::Insert:
        if (operation.range.start < 0 || operation.range.start > frames) {
            setError(error, "insert position is outside the document");
            return false;
        }
        if (operation.payload.empty()) {
            setError(error, "insert audio is empty");
            return false;
        }
        if (operation.payload.numChannels() != m_rendered.numChannels()) {
            setError(error, "insert channel count does not match");
            return false;
        }
        return true;
    case Operation::Kind::MoveRange: {
        if (!validateSampleRange(operation.range, "move")) return false;
        const int64_t destination =
            operation.params.value("destination", int64_t{-1});
        if (destination < 0 || destination > frames) {
            setError(error, "move destination is outside the document");
            return false;
        }
        const bool copy = operation.params.value("copy", false);
        if (!copy && destination > operation.range.start &&
            destination < operation.range.end) {
            setError(error, "move destination overlaps the source range");
            return false;
        }
        return true;
    }
    case Operation::Kind::RepeatRange: {
        if (!validateSampleRange(operation.range, "repeat")) return false;
        const int repeats = operation.params.value("repeats", 0);
        if (repeats <= 0 || repeats > 100000) {
            setError(error, "repeat count must be between 1 and 100000");
            return false;
        }
        if (operation.range.length() >
            std::numeric_limits<int64_t>::max() /
                static_cast<int64_t>(repeats)) {
            setError(error, "repeated range is too large");
            return false;
        }
        return true;
    }
    case Operation::Kind::SetClips:
        return true;
    case Operation::Kind::FlattenClips:
        return true;
    case Operation::Kind::Crop:
        return validateSampleRange(operation.range, "crop");
    case Operation::Kind::Resample:
        if (operation.sampleRate <= 0) {
            setError(error, "invalid target sample rate");
            return false;
        }
        return true;
    case Operation::Kind::ReplaceAll:
        if (operation.payload.empty()) {
            setError(error, "replacement audio is empty");
            return false;
        }
        return true;
    }
    setError(error, "unknown operation");
    return false;
}

bool Document::applyHistoryOperation(const Operation& operation, Audio& base,
                                     std::vector<Clip>& clips,
                                     Selection& after,
                                     std::string* error) const {
    if (operation.kind == Operation::Kind::SetClips) {
        std::vector<Clip> target;
        if (!clipsFromOperation(operation, target, error)) return false;
        if (target.empty()) {
            setError(error, "clip arrangement is empty");
            return false;
        }
        if (!m_clips.validate(target, base.numFrames(), error)) return false;
        clips = std::move(target);
        return true;
    }
    if (operation.kind == Operation::Kind::FlattenClips) {
        if (clips.empty()) {
            setError(error, "no clip arrangement to flatten");
            return false;
        }
        base = ClipArrangement::render(base, clips);
        clips.clear();
        after = {0, base.numFrames(), base.numFrames() > 0};
        return true;
    }
    return applyOperation(operation, base, after, error);
}

bool Document::applyProcess(const Operation& operation, Audio& working,
                            std::string* error) const {
    const Range range = operation.range.clamped(working.numFrames());
    const int channels = working.numChannels();

    switch (operation.process) {
    case Operation::Process::Gain: {
        const double db = operation.params.value("db", 0.0);
        if (!std::isfinite(db)) {
            setError(error, "gain must be finite");
            return false;
        }
        const float gain = static_cast<float>(dbToLinear(db));
        for (int ch = 0; ch < channels; ++ch) {
            float* data = working.channel(ch) + range.start;
            for (int64_t i = 0; i < range.length(); ++i)
                data[i] *= gain;
        }
        return true;
    }
    case Operation::Process::Normalize: {
        const double peakDb = operation.params.value("peakDb", -1.0);
        if (!std::isfinite(peakDb) || peakDb > 0.0) {
            setError(error, "normalize peak must be at most 0 dB");
            return false;
        }
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float* data = working.channel(ch) + range.start;
            for (int64_t i = 0; i < range.length(); ++i)
                peak = std::max(peak, std::abs(data[i]));
        }
        if (peak <= 0.0f) return true;
        const float target = static_cast<float>(dbToLinear(peakDb));
        const float gain = target / peak;
        for (int ch = 0; ch < channels; ++ch) {
            float* data = working.channel(ch) + range.start;
            for (int64_t i = 0; i < range.length(); ++i)
                data[i] *= gain;
        }
        return true;
    }
    case Operation::Process::FadeIn:
    case Operation::Process::FadeOut: {
        const int64_t length = range.length();
        if (length <= 1) return true;
        for (int ch = 0; ch < channels; ++ch) {
            float* data = working.channel(ch) + range.start;
            for (int64_t i = 0; i < length; ++i) {
                const float position =
                    static_cast<float>(i) / static_cast<float>(length - 1);
                const float gain = operation.process == Operation::Process::FadeIn
                                       ? position
                                       : 1.0f - position;
                data[i] *= gain;
            }
        }
        return true;
    }
    case Operation::Process::Reverse: {
        for (int ch = 0; ch < channels; ++ch) {
            float* data = working.channel(ch) + range.start;
            std::reverse(data, data + range.length());
        }
        return true;
    }
    case Operation::Process::Invert: {
        for (int ch = 0; ch < channels; ++ch) {
            float* data = working.channel(ch) + range.start;
            for (int64_t i = 0; i < range.length(); ++i) data[i] = -data[i];
        }
        return true;
    }
    case Operation::Process::Silence: {
        for (int ch = 0; ch < channels; ++ch) {
            std::fill_n(working.channel(ch) + range.start, range.length(), 0.0f);
        }
        return true;
    }
    case Operation::Process::DcRemove: {
        for (int ch = 0; ch < channels; ++ch) {
            float* data = working.channel(ch) + range.start;
            double sum = 0.0;
            for (int64_t i = 0; i < range.length(); ++i)
                sum += static_cast<double>(data[i]);
            const float mean =
                static_cast<float>(sum / static_cast<double>(range.length()));
            for (int64_t i = 0; i < range.length(); ++i) data[i] -= mean;
        }
        return true;
    }
    case Operation::Process::TimeStretch: {
        const double ratio = operation.params.value("ratio", 1.0);
        StretchAlgorithm algorithm = StretchAlgorithm::Wsola;
        stretchAlgorithmFromName(
            operation.params.value("algorithm", std::string{"wsola"}),
            algorithm);
        Audio selected = working.slice(range);
        Audio processed = vwave::timeStretch(selected, ratio, algorithm);
        if (processed.empty() && !selected.empty()) {
            setError(error, "time stretch failed");
            return false;
        }
        Audio result(working.numChannels(),
                     working.numFrames() - range.length() +
                         processed.numFrames(),
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), range.start,
                        result.channel(ch));
            std::copy_n(processed.channel(ch), processed.numFrames(),
                        result.channel(ch) + range.start);
            std::copy_n(working.channel(ch) + range.end,
                        working.numFrames() - range.end,
                        result.channel(ch) + range.start +
                            processed.numFrames());
        }
        working = std::move(result);
        return true;
    }
    case Operation::Process::PitchShift: {
        const double semitones = operation.params.value("semitones", 0.0);
        StretchAlgorithm algorithm = StretchAlgorithm::Wsola;
        stretchAlgorithmFromName(
            operation.params.value("algorithm", std::string{"wsola"}),
            algorithm);
        Audio selected = working.slice(range);
        Audio processed = vwave::pitchShift(selected, semitones, algorithm);
        if (processed.numFrames() != selected.numFrames()) {
            setError(error, "pitch shift produced an invalid length");
            return false;
        }
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(processed.channel(ch), processed.numFrames(),
                        working.channel(ch) + range.start);
        }
        return true;
    }
    case Operation::Process::Convolve: {
        const bool normalizeIr =
            operation.params.value("normalizeIr", false);
        Audio selected = working.slice(range);
        Audio processed = vwave::convolve(selected, operation.payload, normalizeIr);
        if (processed.empty()) {
            setError(error, "convolution failed");
            return false;
        }
        Audio result(working.numChannels(),
                     working.numFrames() - range.length() +
                         processed.numFrames(),
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), range.start,
                        result.channel(ch));
            std::copy_n(processed.channel(ch), processed.numFrames(),
                        result.channel(ch) + range.start);
            std::copy_n(working.channel(ch) + range.end,
                        working.numFrames() - range.end,
                        result.channel(ch) + range.start +
                            processed.numFrames());
        }
        working = std::move(result);
        return true;
    }
    case Operation::Process::SpectralGain: {
        Audio result = vwave::spectralGain(
            working, operation.params.value("lowHz", 0.0),
            operation.params.value("highHz", 0.0),
            operation.params.value("db", 0.0), range);
        if (result.empty()) {
            setError(error, "spectral gain failed");
            return false;
        }
        working = std::move(result);
        return true;
    }
    case Operation::Process::SpectralFilter: {
        Audio result = vwave::spectralFilter(
            working, operation.params.value("type", std::string{}),
            operation.params.value("cutoffHz", 0.0), range);
        if (result.empty()) {
            setError(error, "spectral filter failed");
            return false;
        }
        working = std::move(result);
        return true;
    }
    case Operation::Process::SpectralGate: {
        Audio result = vwave::spectralGate(
            working, operation.params.value("thresholdDb", 0.0), range);
        if (result.empty()) {
            setError(error, "spectral gate failed");
            return false;
        }
        working = std::move(result);
        return true;
    }
    case Operation::Process::SpectralBlur: {
        Audio result = vwave::spectralBlur(
            working, operation.params.value("timeRadius", 0),
            operation.params.value("frequencyRadius", 0), range);
        if (result.empty()) {
            setError(error, "spectral blur failed");
            return false;
        }
        working = std::move(result);
        return true;
    }
    }
    setError(error, "unknown process");
    return false;
}

bool Document::applyOperation(const Operation& operation, Audio& working,
                              Selection& after, std::string* error) const {
    switch (operation.kind) {
    case Operation::Kind::Process: {
        const int64_t oldFrames = working.numFrames();
        if (!applyProcess(operation, working, error)) return false;
        if (working.numFrames() != oldFrames) {
            const int64_t delta = working.numFrames() - oldFrames;
            const int64_t start =
                std::clamp<int64_t>(operation.range.start, 0,
                                    working.numFrames());
            const int64_t end =
                std::clamp<int64_t>(operation.range.end + delta, start,
                                    working.numFrames());
            after = {start, end, end > start};
        }
        return true;
    }
    case Operation::Kind::Delete: {
        const Range range = operation.range;
        Audio result(working.numChannels(),
                     working.numFrames() - range.length(),
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), range.start,
                        result.channel(ch));
            std::copy_n(working.channel(ch) + range.end,
                        working.numFrames() - range.end,
                        result.channel(ch) + range.start);
        }
        working = std::move(result);
        after.clear();
        return true;
    }
    case Operation::Kind::Insert: {
        const int64_t position = operation.range.start;
        const Audio& payload = operation.payload;
        Audio result(working.numChannels(),
                     working.numFrames() + payload.numFrames(),
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), position, result.channel(ch));
            std::copy_n(payload.channel(ch), payload.numFrames(),
                        result.channel(ch) + position);
            std::copy_n(working.channel(ch) + position,
                        working.numFrames() - position,
                        result.channel(ch) + position + payload.numFrames());
        }
        working = std::move(result);
        after = {position, position + payload.numFrames(),
                 payload.numFrames() > 0};
        return true;
    }
    case Operation::Kind::ReplaceRange: {
        const Range range = operation.range;
        const Audio& payload = operation.payload;
        Audio result(working.numChannels(),
                     working.numFrames() - range.length() + payload.numFrames(),
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), range.start,
                        result.channel(ch));
            std::copy_n(payload.channel(ch), payload.numFrames(),
                        result.channel(ch) + range.start);
            std::copy_n(working.channel(ch) + range.end,
                        working.numFrames() - range.end,
                        result.channel(ch) + range.start + payload.numFrames());
        }
        working = std::move(result);
        after = {range.start, range.start + payload.numFrames(),
                 payload.numFrames() > 0};
        return true;
    }
    case Operation::Kind::MoveRange: {
        const Range source = operation.range;
        const int64_t length = source.length();
        const bool copy = operation.params.value("copy", false);
        const int64_t destination =
            operation.params.value("destination", int64_t{0});
        if (copy) {
            Audio result(working.numChannels(),
                         working.numFrames() + length,
                         working.sampleRate());
            for (int ch = 0; ch < working.numChannels(); ++ch) {
                std::copy_n(working.channel(ch), destination,
                            result.channel(ch));
                std::copy_n(working.channel(ch) + source.start, length,
                            result.channel(ch) + destination);
                std::copy_n(working.channel(ch) + destination,
                            working.numFrames() - destination,
                            result.channel(ch) + destination + length);
            }
            working = std::move(result);
            after = {destination, destination + length, length > 0};
            return true;
        }

        Audio rest(working.numChannels(), working.numFrames() - length,
                   working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), source.start, rest.channel(ch));
            std::copy_n(working.channel(ch) + source.end,
                        working.numFrames() - source.end,
                        rest.channel(ch) + source.start);
        }
        const int64_t insertAt =
            destination <= source.start ? destination : destination - length;
        Audio result(working.numChannels(), working.numFrames(),
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(rest.channel(ch), insertAt, result.channel(ch));
            std::copy_n(working.channel(ch) + source.start, length,
                        result.channel(ch) + insertAt);
            std::copy_n(rest.channel(ch) + insertAt,
                        rest.numFrames() - insertAt,
                        result.channel(ch) + insertAt + length);
        }
        working = std::move(result);
        after = {insertAt, insertAt + length, length > 0};
        return true;
    }
    case Operation::Kind::RepeatRange: {
        const Range source = operation.range;
        const int64_t length = source.length();
        const int repeats = operation.params.value("repeats", 1);
        const int64_t added = length * static_cast<int64_t>(repeats);
        Audio result(working.numChannels(), working.numFrames() + added,
                     working.sampleRate());
        for (int ch = 0; ch < working.numChannels(); ++ch) {
            std::copy_n(working.channel(ch), source.end, result.channel(ch));
            for (int repeat = 0; repeat < repeats; ++repeat) {
                std::copy_n(working.channel(ch) + source.start, length,
                            result.channel(ch) + source.end +
                                static_cast<int64_t>(repeat) * length);
            }
            std::copy_n(working.channel(ch) + source.end,
                        working.numFrames() - source.end,
                        result.channel(ch) + source.end + added);
        }
        working = std::move(result);
        after = {source.start, source.end + added, added > 0};
        return true;
    }
    case Operation::Kind::SetClips:
    case Operation::Kind::FlattenClips:
        setError(error, "clip operation requires arrangement state");
        return false;
    case Operation::Kind::Crop:
        working = working.slice(operation.range);
        after = {0, working.numFrames(), working.numFrames() > 0};
        return true;
    case Operation::Kind::Resample: {
        const int64_t oldFrames = working.numFrames();
        Audio result = vwave::resample(working, operation.sampleRate);
        if (result.empty() && !working.empty()) {
            setError(error, "resampling failed");
            return false;
        }
        if (after.active && oldFrames > 0) {
            const double scale = static_cast<double>(result.numFrames()) /
                                 static_cast<double>(oldFrames);
            after.start = static_cast<int64_t>(
                std::llround(static_cast<double>(after.start) * scale));
            after.end = static_cast<int64_t>(
                std::llround(static_cast<double>(after.end) * scale));
            after.active = after.end > after.start;
        }
        working = std::move(result);
        return true;
    }
    case Operation::Kind::ReplaceAll:
        working = operation.payload;
        after = {0, working.numFrames(), working.numFrames() > 0};
        return true;
    }
    setError(error, "unknown operation kind");
    return false;
}

bool Document::apply(const Operation& operation, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();

    if (operation.kind == Operation::Kind::SetClips) {
        std::vector<Clip> clips;
        if (!clipsFromOperation(operation, clips, error)) return false;
        if (clips.empty()) {
            setError(error, "clip arrangement is empty");
            return false;
        }
        if (!m_clips.validate(clips, m_rendered.numFrames(), error))
            return false;
        const Selection before = m_selection;
        m_clips.assign(std::move(clips));
        refreshArranged();
        m_selection = clampSelection(m_selection, m_arranged.numFrames());
        m_history.push_back({operation, before, m_selection});
        m_redo.clear();
        return true;
    }

    if (operation.kind == Operation::Kind::FlattenClips) {
        if (m_clips.empty()) {
            setError(error, "no clip arrangement to flatten");
            return false;
        }
        const Selection before = m_selection;
        m_rendered = m_clips.render(m_rendered);
        m_clips.clear();
        m_arranged = m_rendered;
        m_arrangedValid = true;
        m_cacheValid = true;
        m_selection = clampSelection(m_selection, m_arranged.numFrames());
        m_history.push_back({operation, before, m_selection});
        m_redo.clear();
        return true;
    }

    if (!m_clips.empty()) {
        setError(error, "flatten the clip arrangement before destructive edits");
        return false;
    }
    if (!validateRange(operation, error)) return false;

    Audio working = m_rendered;
    Selection before = m_selection;
    Selection after = m_selection;
    if (!applyOperation(operation, working, after, error)) return false;

    m_rendered = std::move(working);
    m_arranged = m_rendered;
    m_cacheValid = true;
    m_arrangedValid = true;
    m_selection = clampSelection(after, m_rendered.numFrames());
    m_history.push_back({operation, before, m_selection});
    m_redo.clear();
    return true;
}

bool Document::undo() {
    if (!m_cacheValid) rebuild();
    if (m_history.empty()) return false;
    HistoryEntry entry = std::move(m_history.back());
    m_history.pop_back();
    m_redo.push_back(entry);
    if (!rebuild()) return false;
    m_selection = clampSelection(entry.before, m_rendered.numFrames());
    return true;
}

bool Document::redo() {
    if (!m_cacheValid) rebuild();
    if (m_redo.empty()) return false;
    HistoryEntry entry = std::move(m_redo.back());
    m_redo.pop_back();

    Audio base = m_rendered;
    std::vector<Clip> clips = m_clips.clips();
    Selection after = entry.after;
    if (!applyHistoryOperation(entry.operation, base, clips, after, nullptr)) {
        m_redo.push_back(std::move(entry));
        return false;
    }
    m_rendered = std::move(base);
    m_clips.assign(std::move(clips));
    m_arranged = m_clips.render(m_rendered);
    m_arrangedValid = true;
    m_cacheValid = true;
    m_history.push_back(entry);
    m_selection = clampSelection(entry.after, m_arranged.numFrames());
    return true;
}

std::string Document::undoLabel() const {
    return m_history.empty() ? std::string{} : m_history.back().operation.label;
}

std::string Document::redoLabel() const {
    return m_redo.empty() ? std::string{} : m_redo.back().operation.label;
}

void Document::clearHistory() {
    if (!m_cacheValid) rebuild();
    if (!m_clips.empty()) m_rendered = m_clips.render(m_rendered);
    m_source = m_rendered;
    m_clips.clear();
    m_arranged = m_rendered;
    m_history.clear();
    m_redo.clear();
    m_cacheValid = true;
    m_arrangedValid = true;
    m_selection = clampSelection(m_selection, m_source.numFrames());
}

bool Document::gainDb(double db, std::string* error) {
    return apply(Operation::gain(db, activeRange()), error);
}

bool Document::normalize(double peakDb, std::string* error) {
    return apply(Operation::normalize(peakDb, activeRange()), error);
}

bool Document::fadeIn(std::string* error) {
    return apply(Operation::fadeIn(activeRange()), error);
}

bool Document::fadeOut(std::string* error) {
    return apply(Operation::fadeOut(activeRange()), error);
}

bool Document::reverseSelection(std::string* error) {
    return apply(Operation::reverse(activeRange()), error);
}

bool Document::invertSelection(std::string* error) {
    return apply(Operation::invert(activeRange()), error);
}

bool Document::silenceSelection(std::string* error) {
    return apply(Operation::silence(activeRange()), error);
}

bool Document::removeDc(std::string* error) {
    return apply(Operation::dcRemove(activeRange()), error);
}

bool Document::timeStretch(double ratio, StretchAlgorithm algorithm,
                           std::string* error) {
    return apply(Operation::timeStretch(ratio, activeRange(), algorithm), error);
}

bool Document::timeStretchTrack(double ratio, StretchAlgorithm algorithm,
                                std::string* error) {
    if (!m_cacheValid) rebuild();
    if (m_rendered.empty()) {
        setError(error, "document is empty");
        return false;
    }
    return apply(Operation::timeStretch(ratio, {0, m_rendered.numFrames()},
                                        algorithm),
                 error);
}

bool Document::pitchShift(double semitones, StretchAlgorithm algorithm,
                          std::string* error) {
    return apply(Operation::pitchShift(semitones, activeRange(), algorithm),
                 error);
}

bool Document::spectralGain(double lowHz, double highHz, double gainDb,
                            std::string* error) {
    return apply(
        Operation::spectralGain(lowHz, highHz, gainDb, activeRange()), error);
}

bool Document::spectralFilter(const std::string& type, double cutoffHz,
                              std::string* error) {
    return apply(Operation::spectralFilter(type, cutoffHz, activeRange()),
                 error);
}

bool Document::spectralGate(double thresholdDb, std::string* error) {
    return apply(Operation::spectralGate(thresholdDb, activeRange()), error);
}

bool Document::spectralBlur(int timeRadius, int frequencyRadius,
                            std::string* error) {
    return apply(
        Operation::spectralBlur(timeRadius, frequencyRadius, activeRange()),
        error);
}

bool Document::convolve(const Audio& impulseResponse, bool normalizeIr,
                        std::string* error) {
    return apply(Operation::convolve(impulseResponse, activeRange(), normalizeIr),
                 error);
}

bool Document::copySelection() {
    if (!m_cacheValid) rebuild();
    if (!m_selection.active || m_selection.empty()) return false;
    m_clipboard = audio().slice(m_selection.range());
    return true;
}

bool Document::cutSelection(std::string* error) {
    if (!m_cacheValid) rebuild();
    if (!m_selection.active || m_selection.empty()) return false;
    if (!copySelection()) return false;
    return deleteSelection(error);
}

bool Document::deleteSelection(std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::remove(m_selection.range()), error);
}

bool Document::moveSelectionTo(int64_t destination, bool copy,
                               std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::moveRange(m_selection.range(), destination, copy),
                 error);
}

bool Document::repeatSelection(int repeats, std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::repeatRange(m_selection.range(), repeats), error);
}

bool Document::paste(std::string* error) {
    if (m_clipboard.empty()) {
        setError(error, "clipboard is empty");
        return false;
    }
    if (m_selection.active && !m_selection.empty()) {
        return apply(Operation::replaceRange(m_selection.range(), m_clipboard),
                     error);
    }
    return apply(
        Operation::insert(m_rendered.numFrames(), m_clipboard), error);
}

bool Document::pasteAt(int64_t frame, std::string* error) {
    if (m_clipboard.empty()) {
        setError(error, "clipboard is empty");
        return false;
    }
    return apply(Operation::insert(frame, m_clipboard), error);
}

bool Document::cropToSelection(std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::crop(m_selection.range()), error);
}

bool Document::insertAudio(int64_t frame, Audio audio, std::string* error) {
    return apply(Operation::insert(frame, std::move(audio)), error);
}

bool Document::replaceSelection(Audio audio, std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::replaceRange(m_selection.range(), std::move(audio)),
                 error);
}

bool Document::resample(int sampleRate, std::string* error) {
    return apply(Operation::resample(sampleRate), error);
}

void Document::clearClipboard() { m_clipboard = Audio{}; }

bool Document::addMarker(int64_t frame, std::string name) {
    if (frame < 0 || frame > numFrames()) return false;
    if (name.empty()) name = "Marker " + std::to_string(m_markers.size() + 1);
    m_markers.push_back({frame, std::move(name)});
    std::stable_sort(m_markers.begin(), m_markers.end(),
                     [](const Marker& a, const Marker& b) {
                         return a.frame < b.frame;
                     });
    return true;
}

bool Document::removeMarker(size_t index) {
    if (index >= m_markers.size()) return false;
    m_markers.erase(m_markers.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void Document::clearMarkers() { m_markers.clear(); }

void Document::setSlices(std::vector<Slice> slices) {
    m_slices = std::move(slices);
}

void Document::clearSlices() { m_slices.clear(); }

bool Document::loadAudio(const std::string& path, std::string* error) {
    if (error) error->clear();
    Audio loaded;
    if (!readAudio(path, loaded, error)) return false;
    if (loaded.empty()) {
        setError(error, "audio file is empty");
        return false;
    }
    m_source = std::move(loaded);
    m_sourcePath = path;
    m_history.clear();
    m_redo.clear();
    m_rendered = m_source;
    m_arranged = m_rendered;
    m_clips.clear();
    m_cacheValid = true;
    m_arrangedValid = true;
    m_selection.clear();
    m_clipboard = Audio{};
    m_markers.clear();
    m_slices.clear();
    return true;
}

bool Document::saveAudio(const std::string& path,
                         const AudioExportOptions& options,
                         std::string* error) const {
    if (error) error->clear();
    if (audio().empty()) {
        setError(error, "document is empty");
        return false;
    }
    return writeAudio(path, audio(), options, error);
}

nlohmann::json Document::toJson() const {
    if (m_source.empty())
        throw std::runtime_error("document has no source audio");
    nlohmann::json root;
    root["format"] = "vwave-project";
    root["version"] = 1;
    root["source"] = audioToJson(m_source);
    root["sourcePath"] = m_sourcePath;
    root["selection"] = selectionToJson(m_selection);
    root["markers"] = nlohmann::json::array();
    for (const auto& marker : m_markers)
        root["markers"].push_back(
            {{"frame", marker.frame}, {"name", marker.name}});
    root["slices"] = nlohmann::json::array();
    for (const auto& slice : m_slices)
        root["slices"].push_back(
            {{"start", slice.range.start},
             {"end", slice.range.end},
             {"name", slice.name}});
    root["history"] = nlohmann::json::array();
    for (const auto& entry : m_history) {
        root["history"].push_back(
            {{"operation", entry.operation.toJson()},
             {"before", selectionToJson(entry.before)},
             {"after", selectionToJson(entry.after)}});
    }
    return root;
}

bool Document::fromJson(const nlohmann::json& root, Document& out,
                        std::string* error) {
    if (error) error->clear();
    try {
        if (root.value("format", std::string{}) != "vwave-project") {
            setError(error, "not a VWaveEditor project");
            return false;
        }
        if (root.value("version", 0) != 1) {
            setError(error, "unsupported project version");
            return false;
        }

        Audio source;
        if (!audioFromJson(root.at("source"), source, error)) return false;
        if (source.empty()) {
            setError(error, "project source audio is empty");
            return false;
        }

        Document loaded(std::move(source));
        loaded.m_sourcePath = root.value("sourcePath", std::string{});
        loaded.m_selection = selectionFromJson(
            root.value("selection", nlohmann::json{}));

        if (root.contains("markers") && root["markers"].is_array()) {
            for (const auto& marker : root["markers"]) {
                const int64_t frame = marker.value("frame", int64_t{0});
                if (frame < 0) continue;
                loaded.m_markers.push_back(
                    {frame, marker.value("name", std::string{})});
            }
            std::stable_sort(
                loaded.m_markers.begin(), loaded.m_markers.end(),
                [](const Marker& a, const Marker& b) {
                    return a.frame < b.frame;
                });
        }
        if (root.contains("slices") && root["slices"].is_array()) {
            for (const auto& slice : root["slices"]) {
                const int64_t start = slice.value("start", int64_t{0});
                const int64_t end = slice.value("end", int64_t{0});
                if (start < 0 || end <= start) continue;
                loaded.m_slices.push_back(
                    {{start, end}, slice.value("name", std::string{})});
            }
        }

        if (root.contains("history") && root["history"].is_array()) {
            Audio working = loaded.m_source;
            std::vector<Clip> clips;
            for (const auto& item : root["history"]) {
                Operation operation;
                if (!Operation::fromJson(item.at("operation"), operation,
                                         error))
                    return false;
                loaded.m_rendered = working;
                loaded.m_clips.assign(clips);
                loaded.m_cacheValid = true;
                if (!loaded.validateRange(operation, error)) return false;

                Selection after = selectionFromJson(
                    item.value("after", nlohmann::json{}));
                if (!loaded.applyHistoryOperation(operation, working, clips,
                                                  after, error)) {
                    setError(error, "cannot replay project operation: " +
                                        operation.label + ": " +
                                        (error ? *error : std::string{}));
                    return false;
                }
                Selection before = selectionFromJson(
                    item.value("before", nlohmann::json{}));
                loaded.m_history.push_back(
                    {std::move(operation), before, after});
            }
            loaded.m_rendered = std::move(working);
            loaded.m_clips.assign(std::move(clips));
            loaded.m_arranged = loaded.m_clips.render(loaded.m_rendered);
            loaded.m_arrangedValid = true;
            loaded.m_cacheValid = true;
            loaded.m_redo.clear();
        } else {
            loaded.m_rendered = loaded.m_source;
            loaded.m_arranged = loaded.m_rendered;
            loaded.m_cacheValid = true;
            loaded.m_arrangedValid = true;
        }

        const int64_t arrangedFrames = loaded.m_arranged.numFrames();
        loaded.m_markers.erase(
            std::remove_if(loaded.m_markers.begin(), loaded.m_markers.end(),
                           [arrangedFrames](const Marker& marker) {
                               return marker.frame > arrangedFrames;
                           }),
            loaded.m_markers.end());
        loaded.m_slices.erase(
            std::remove_if(loaded.m_slices.begin(), loaded.m_slices.end(),
                           [arrangedFrames](const Slice& slice) {
                               return slice.range.end > arrangedFrames;
                           }),
            loaded.m_slices.end());

        out = std::move(loaded);
        out.m_selection =
            clampSelection(out.m_selection, out.m_arranged.numFrames());
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

bool Document::saveProject(const std::string& path, std::string* error) const {
    if (error) error->clear();
    if (m_source.empty()) {
        setError(error, "document has no source audio");
        return false;
    }
    try {
        const nlohmann::json root = toJson();
        std::ofstream outFile(path, std::ios::binary);
        if (!outFile) {
            setError(error, "cannot create project file");
            return false;
        }
        outFile << root.dump(2) << '\n';
        if (!outFile.good()) {
            setError(error, "failed while writing project file");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot save project: ") + e.what());
        return false;
    }
}

bool Document::loadProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            setError(error, "cannot open project file");
            return false;
        }
        nlohmann::json root;
        in >> root;
        return fromJson(root, *this, error);
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

bool Document::rebuild() {
    Audio working = m_source;
    std::vector<Clip> clips;
    Selection selection;
    for (const auto& entry : m_history) {
        Selection after = selection;
        if (!applyHistoryOperation(entry.operation, working, clips, after,
                                   nullptr)) {
            m_rendered = std::move(working);
            m_clips.assign(std::move(clips));
            m_arranged = m_clips.render(m_rendered);
            m_arrangedValid = true;
            m_cacheValid = true;
            return false;
        }
        selection = after;
    }
    m_rendered = std::move(working);
    m_clips.assign(std::move(clips));
    m_arranged = m_clips.render(m_rendered);
    m_arrangedValid = true;
    m_cacheValid = true;
    m_selection = clampSelection(m_selection, m_arranged.numFrames());
    return true;
}

} // namespace vwave
