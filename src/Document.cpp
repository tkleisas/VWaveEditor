#include "vwave/Document.h"

#include "DocumentInternal.h"
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

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

} // namespace

using namespace detail;

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
