#include "vwave/ClipArrangement.h"

#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vwave {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

} // namespace

int64_t clipOutputFrames(const Clip& clip) {
    if (clip.sourceLength <= 0 || !std::isfinite(clip.stretch) ||
        clip.stretch <= 0.0)
        return 0;
    const double value =
        static_cast<double>(clip.sourceLength) * clip.stretch;
    if (value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
        return 0;
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(value)));
}

nlohmann::json clipToJson(const Clip& clip) {
    return {{"sourceStart", clip.sourceStart},
            {"sourceLength", clip.sourceLength},
            {"timelineStart", clip.timelineStart},
            {"stretch", clip.stretch},
            {"gainDb", clip.gainDb},
            {"fadeInSeconds", clip.fadeInSeconds},
            {"fadeOutSeconds", clip.fadeOutSeconds},
            {"name", clip.name}};
}

bool clipFromJson(const nlohmann::json& json, Clip& out,
                  std::string* error) {
    try {
        if (!json.is_object()) {
            setError(error, "clip must be an object");
            return false;
        }
        Clip clip;
        clip.sourceStart = json.value("sourceStart", int64_t{0});
        clip.sourceLength = json.value("sourceLength", int64_t{0});
        clip.timelineStart = json.value("timelineStart", int64_t{0});
        clip.stretch = json.value("stretch", 1.0);
        clip.gainDb = json.value("gainDb", 0.0);
        clip.fadeInSeconds = json.value("fadeInSeconds", 0.0);
        clip.fadeOutSeconds = json.value("fadeOutSeconds", 0.0);
        clip.name = json.value("name", std::string{});
        out = std::move(clip);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot parse clip: ") + e.what());
        return false;
    }
}

int ClipArrangement::indexAt(int64_t frame) const {
    for (size_t i = 0; i < m_clips.size(); ++i) {
        const Clip& clip = m_clips[i];
        const int64_t end = clip.timelineStart + clipOutputFrames(clip);
        if (frame >= clip.timelineStart && frame < end)
            return static_cast<int>(i);
    }
    return -1;
}

bool ClipArrangement::validate(const std::vector<Clip>& clips,
                               int64_t baseFrames,
                               std::string* error) const {
    for (const Clip& clip : clips) {
        if (clip.sourceStart < 0 || clip.sourceLength <= 0 ||
            clip.sourceStart + clip.sourceLength > baseFrames) {
            setError(error, "clip source range is outside the document");
            return false;
        }
        if (clip.timelineStart < 0) {
            setError(error, "clip timeline position must be non-negative");
            return false;
        }
        const int64_t outputFrames = clipOutputFrames(clip);
        if (!std::isfinite(clip.stretch) || clip.stretch <= 0.0 ||
            outputFrames <= 0 ||
            clip.timelineStart >
                std::numeric_limits<int64_t>::max() - outputFrames) {
            setError(error, "clip has an invalid stretch ratio or position");
            return false;
        }
        if (!std::isfinite(clip.gainDb) ||
            !std::isfinite(clip.fadeInSeconds) ||
            !std::isfinite(clip.fadeOutSeconds) ||
            clip.fadeInSeconds < 0.0 || clip.fadeOutSeconds < 0.0) {
            setError(error, "clip has invalid gain or fade values");
            return false;
        }
    }
    return true;
}

Audio ClipArrangement::render(const Audio& base,
                              const std::vector<Clip>& clips) {
    if (clips.empty()) return base;
    if (base.empty()) return Audio{};

    int64_t totalFrames = 0;
    for (const auto& clip : clips) {
        const int64_t frames = clipOutputFrames(clip);
        if (frames <= 0) continue;
        totalFrames = std::max(totalFrames, clip.timelineStart + frames);
    }
    if (totalFrames <= 0)
        return Audio(base.numChannels(), 0, base.sampleRate());

    Audio output(base.numChannels(), totalFrames, base.sampleRate());
    for (const auto& clip : clips) {
        if (clip.sourceLength <= 0 || clip.timelineStart < 0) continue;
        const int64_t expectedFrames = clipOutputFrames(clip);
        if (expectedFrames <= 0) continue;
        Audio segment = base.slice(
            {clip.sourceStart, clip.sourceStart + clip.sourceLength});
        if (std::abs(clip.stretch - 1.0) > 1e-9) {
            segment = vwave::timeStretch(segment, clip.stretch,
                                         StretchAlgorithm::Wsola);
        }
        if (segment.empty()) continue;
        const int64_t frames =
            std::min<int64_t>(segment.numFrames(),
                              totalFrames - clip.timelineStart);
        if (frames <= 0) continue;
        const int64_t fadeInFrames = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                clip.fadeInSeconds * base.sampleRate())),
            0, frames);
        const int64_t fadeOutFrames = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                clip.fadeOutSeconds * base.sampleRate())),
            0, frames);
        const double gain =
            std::pow(10.0, std::clamp(clip.gainDb, -96.0, 24.0) / 20.0);
        for (int64_t frame = 0; frame < frames; ++frame) {
            double envelope = gain;
            if (fadeInFrames > 0 && frame < fadeInFrames) {
                envelope *= static_cast<double>(frame) /
                            static_cast<double>(fadeInFrames);
            }
            if (fadeOutFrames > 0 &&
                frame >= frames - fadeOutFrames) {
                const double position =
                    fadeOutFrames > 1
                        ? static_cast<double>(frame -
                                              (frames - fadeOutFrames)) /
                              static_cast<double>(fadeOutFrames - 1)
                        : 1.0;
                envelope *= 1.0 - position;
            }
            for (int ch = 0; ch < output.numChannels(); ++ch) {
                output.sample(ch, clip.timelineStart + frame) +=
                    static_cast<float>(segment.sample(ch, frame) * envelope);
            }
        }
    }
    return output;
}

Audio ClipArrangement::render(const Audio& base) const {
    return render(base, m_clips);
}

bool ClipArrangement::makeFull(int64_t baseFrames,
                               std::vector<Clip>& out,
                               std::string* error) const {
    if (baseFrames <= 0) {
        setError(error, "document is empty");
        return false;
    }
    Clip clip;
    clip.sourceLength = baseFrames;
    clip.name = "Clip 1";
    out = {std::move(clip)};
    return true;
}

bool ClipArrangement::splitAt(int64_t frame, int64_t baseFrames,
                              std::vector<Clip>& out,
                              std::string* error) const {
    out = m_clips;
    if (out.empty()) {
        if (!makeFull(baseFrames, out, error)) return false;
    }
    int index = -1;
    for (size_t i = 0; i < out.size(); ++i) {
        const int64_t end =
            out[i].timelineStart + clipOutputFrames(out[i]);
        if (frame > out[i].timelineStart && frame < end) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        setError(error, "split point is not inside a clip");
        return false;
    }
    Clip& clip = out[static_cast<size_t>(index)];
    if (clip.sourceLength < 2) {
        setError(error, "clip is too short to split");
        return false;
    }
    const int64_t local = frame - clip.timelineStart;
    const int64_t sourceOffset = std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(
            static_cast<double>(local) / clip.stretch)),
        1, clip.sourceLength - 1);

    Clip left = clip;
    left.sourceLength = sourceOffset;
    left.fadeOutSeconds = 0.0;
    Clip right = clip;
    right.sourceStart += sourceOffset;
    right.sourceLength -= sourceOffset;
    right.timelineStart = clip.timelineStart + clipOutputFrames(left);
    right.fadeInSeconds = 0.0;
    if (!left.name.empty()) left.name += " A";
    if (!right.name.empty()) right.name += " B";
    out[static_cast<size_t>(index)] = std::move(left);
    out.insert(out.begin() + index + 1, std::move(right));
    return true;
}

bool ClipArrangement::remove(size_t index, std::vector<Clip>& out,
                             std::string* error) const {
    if (index >= m_clips.size()) {
        setError(error, "clip index is out of range");
        return false;
    }
    if (m_clips.size() <= 1) {
        setError(error, "cannot delete the last clip; flatten instead");
        return false;
    }
    out = m_clips;
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool ClipArrangement::move(size_t index, int64_t timelineStart,
                           std::vector<Clip>& out,
                           std::string* error) const {
    if (index >= m_clips.size()) {
        setError(error, "clip index is out of range");
        return false;
    }
    if (timelineStart < 0) {
        setError(error, "clip position must be non-negative");
        return false;
    }
    out = m_clips;
    out[index].timelineStart = timelineStart;
    return true;
}

bool ClipArrangement::stretch(size_t index, double ratio,
                              std::vector<Clip>& out,
                              std::string* error) const {
    if (index >= m_clips.size()) {
        setError(error, "clip index is out of range");
        return false;
    }
    if (!std::isfinite(ratio) || ratio <= 0.0) {
        setError(error, "clip stretch ratio must be positive");
        return false;
    }
    out = m_clips;
    out[index].stretch = std::clamp(ratio, 0.05, 20.0);
    return true;
}

bool ClipArrangement::repeat(size_t index, int count,
                             std::vector<Clip>& out,
                             std::string* error) const {
    if (index >= m_clips.size()) {
        setError(error, "clip index is out of range");
        return false;
    }
    if (count <= 0 || count > 1000) {
        setError(error, "clip repeat count must be between 1 and 1000");
        return false;
    }
    const Clip& source = m_clips[index];
    const int64_t length = clipOutputFrames(source);
    const int64_t end = source.timelineStart + length;
    if (length > 0 &&
        count > std::numeric_limits<int64_t>::max() / length) {
        setError(error, "repeated clip is too large");
        return false;
    }
    const int64_t added = length * static_cast<int64_t>(count);
    out.clear();
    out.reserve(m_clips.size() + static_cast<size_t>(count));
    for (size_t i = 0; i < m_clips.size(); ++i) {
        Clip clip = m_clips[i];
        if (i == index) {
            out.push_back(clip);
            for (int repeat = 1; repeat <= count; ++repeat) {
                Clip copy = clip;
                copy.timelineStart =
                    clip.timelineStart + static_cast<int64_t>(repeat) * length;
                if (!copy.name.empty())
                    copy.name += " copy " + std::to_string(repeat);
                out.push_back(std::move(copy));
            }
        } else {
            if (clip.timelineStart >= end) clip.timelineStart += added;
            out.push_back(std::move(clip));
        }
    }
    return true;
}

bool ClipArrangement::setGain(size_t index, double gainDb,
                              std::vector<Clip>& out,
                              std::string* error) const {
    if (index >= m_clips.size()) {
        setError(error, "clip index is out of range");
        return false;
    }
    if (!std::isfinite(gainDb)) {
        setError(error, "clip gain must be finite");
        return false;
    }
    out = m_clips;
    out[index].gainDb = std::clamp(gainDb, -96.0, 24.0);
    return true;
}

bool ClipArrangement::setFades(size_t index, double fadeInSeconds,
                               double fadeOutSeconds,
                               std::vector<Clip>& out,
                               std::string* error) const {
    if (index >= m_clips.size()) {
        setError(error, "clip index is out of range");
        return false;
    }
    if (!std::isfinite(fadeInSeconds) || !std::isfinite(fadeOutSeconds) ||
        fadeInSeconds < 0.0 || fadeOutSeconds < 0.0) {
        setError(error, "clip fades must be non-negative");
        return false;
    }
    out = m_clips;
    out[index].fadeInSeconds = fadeInSeconds;
    out[index].fadeOutSeconds = fadeOutSeconds;
    return true;
}

} // namespace vwave
