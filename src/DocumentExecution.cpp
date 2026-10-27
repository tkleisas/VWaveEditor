#include "vwave/Document.h"

#include "DocumentInternal.h"

#include "vwave/Processing.h"
#include "vwave/Spectral.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

namespace {

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

} // namespace

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

} // namespace vwave
