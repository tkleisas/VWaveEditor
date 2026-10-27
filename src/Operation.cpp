#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

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

} // namespace vwave
