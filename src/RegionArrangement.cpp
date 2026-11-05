#include "vwave/RegionArrangement.h"

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

int64_t regionOutputFrames(const AudioRegion& region) {
    if (region.sourceLength <= 0 || !std::isfinite(region.stretch) ||
        region.stretch <= 0.0)
        return 0;
    const double value =
        static_cast<double>(region.sourceLength) * region.stretch;
    if (value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
        return 0;
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(value)));
}

nlohmann::json audioRegionToJson(const AudioRegion& region) {
    return {{"block", region.block},
            {"sourceStart", region.sourceStart},
            {"sourceLength", region.sourceLength},
            {"timelineStart", region.timelineStart},
            {"stretch", region.stretch},
            {"gainDb", region.gainDb},
            {"fadeInSeconds", region.fadeInSeconds},
            {"fadeOutSeconds", region.fadeOutSeconds},
            {"name", region.name}};
}

bool audioRegionFromJson(const nlohmann::json& json, AudioRegion& out,
                         std::string* error) {
    if (error) error->clear();
    if (!json.is_object()) {
        setError(error, "audio region must be an object");
        return false;
    }
    try {
        AudioRegion region;
        region.block = json.value("block", uint64_t{0});
        region.sourceStart = json.value("sourceStart", int64_t{0});
        region.sourceLength = json.value("sourceLength", int64_t{0});
        region.timelineStart = json.value("timelineStart", int64_t{0});
        region.stretch = json.value("stretch", 1.0);
        region.gainDb = json.value("gainDb", 0.0);
        region.fadeInSeconds = json.value("fadeInSeconds", 0.0);
        region.fadeOutSeconds = json.value("fadeOutSeconds", 0.0);
        region.name = json.value("name", std::string{});
        if (region.block == kInvalidAudioBlock) {
            setError(error, "audio region has no block");
            return false;
        }
        out = std::move(region);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot parse audio region: ") + e.what());
        return false;
    }
}

int RegionArrangement::indexAt(int64_t frame) const {
    for (size_t i = 0; i < m_regions.size(); ++i) {
        const AudioRegion& region = m_regions[i];
        const int64_t end =
            region.timelineStart + regionOutputFrames(region);
        if (frame >= region.timelineStart && frame < end)
            return static_cast<int>(i);
    }
    return -1;
}

int64_t RegionArrangement::durationFrames() const {
    int64_t duration = 0;
    for (const auto& region : m_regions) {
        const int64_t frames = regionOutputFrames(region);
        if (frames <= 0) continue;
        if (region.timelineStart >
            std::numeric_limits<int64_t>::max() - frames)
            return std::numeric_limits<int64_t>::max();
        duration = std::max(duration, region.timelineStart + frames);
    }
    return duration;
}

bool RegionArrangement::validate(const AudioBlockStore& store,
                                 std::string* error) const {
    return validate(m_regions, store, error);
}

bool RegionArrangement::validate(const std::vector<AudioRegion>& regions,
                                 const AudioBlockStore& store,
                                 std::string* error) const {
    if (error) error->clear();
    int channels = 0;
    int sampleRate = 0;
    for (const auto& region : regions) {
        if (region.block == kInvalidAudioBlock) {
            setError(error, "audio region has no block");
            return false;
        }
        const AudioBlockInfo info = store.info(region.block);
        if (info.id == kInvalidAudioBlock) {
            setError(error, "audio region references a missing block");
            return false;
        }
        if (info.channels <= 0 || info.sampleRate <= 0) {
            setError(error, "audio region references an empty block");
            return false;
        }
        if (channels == 0) {
            channels = info.channels;
            sampleRate = info.sampleRate;
        } else if (channels != info.channels ||
                   sampleRate != info.sampleRate) {
            setError(error, "audio region blocks must share a format");
            return false;
        }
        if (region.sourceStart < 0 || region.sourceLength <= 0 ||
            region.sourceStart >
                std::numeric_limits<int64_t>::max() - region.sourceLength ||
            region.sourceStart + region.sourceLength > info.frames) {
            setError(error, "audio region source range is outside its block");
            return false;
        }
        if (region.timelineStart < 0) {
            setError(error, "audio region timeline position is negative");
            return false;
        }
        const int64_t outputFrames = regionOutputFrames(region);
        if (outputFrames <= 0 ||
            !std::isfinite(region.stretch) || region.stretch <= 0.0) {
            setError(error, "audio region has an invalid stretch ratio");
            return false;
        }
        if (region.timelineStart >
            std::numeric_limits<int64_t>::max() - outputFrames) {
            setError(error, "audio region timeline position overflows");
            return false;
        }
        if (!std::isfinite(region.gainDb) ||
            !std::isfinite(region.fadeInSeconds) ||
            !std::isfinite(region.fadeOutSeconds) ||
            region.fadeInSeconds < 0.0 || region.fadeOutSeconds < 0.0) {
            setError(error, "audio region has invalid gain or fades");
            return false;
        }
    }
    return true;
}

Audio RegionArrangement::render(const AudioBlockStore& store,
                                std::string* error) const {
    return render(store, m_regions, error);
}

Audio RegionArrangement::render(const AudioBlockStore& store,
                                const std::vector<AudioRegion>& regions,
                                std::string* error) {
    if (error) error->clear();
    RegionArrangement validator;
    if (!validator.validate(regions, store, error)) return {};
    if (regions.empty()) return {};

    const AudioBlockInfo first = store.info(regions.front().block);
    int64_t totalFrames = 0;
    for (const auto& region : regions) {
        const int64_t frames = regionOutputFrames(region);
        if (frames <= 0) continue;
        if (region.timelineStart >
            std::numeric_limits<int64_t>::max() - frames) {
            setError(error, "audio region timeline position overflows");
            return {};
        }
        totalFrames = std::max(totalFrames, region.timelineStart + frames);
    }
    if (totalFrames <= 0) return {};

    Audio output(first.channels, totalFrames, first.sampleRate);
    for (const auto& region : regions) {
        Audio block;
        if (!store.tryGet(region.block, block, error)) return {};
        Audio segment = block.slice(
            {region.sourceStart, region.sourceStart + region.sourceLength});
        if (std::abs(region.stretch - 1.0) > 1e-9) {
            segment = vwave::timeStretch(segment, region.stretch,
                                         StretchAlgorithm::Wsola);
        }
        if (segment.empty()) continue;
        const int64_t frames =
            std::min<int64_t>(segment.numFrames(),
                              totalFrames - region.timelineStart);
        if (frames <= 0) continue;
        const int64_t fadeInFrames = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                region.fadeInSeconds * first.sampleRate)),
            0, frames);
        const int64_t fadeOutFrames = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                region.fadeOutSeconds * first.sampleRate)),
            0, frames);
        const double gain =
            std::pow(10.0, std::clamp(region.gainDb, -96.0, 24.0) / 20.0);
        for (int64_t frame = 0; frame < frames; ++frame) {
            double envelope = gain;
            if (fadeInFrames > 0 && frame < fadeInFrames) {
                envelope *= static_cast<double>(frame) /
                            static_cast<double>(fadeInFrames);
            }
            if (fadeOutFrames > 0 && frame >= frames - fadeOutFrames) {
                const double position =
                    fadeOutFrames > 1
                        ? static_cast<double>(
                              frame - (frames - fadeOutFrames)) /
                              static_cast<double>(fadeOutFrames - 1)
                        : 1.0;
                envelope *= 1.0 - position;
            }
            for (int ch = 0; ch < output.numChannels(); ++ch) {
                output.sample(ch, region.timelineStart + frame) +=
                    static_cast<float>(segment.sample(ch, frame) * envelope);
            }
        }
    }
    return output;
}

bool RegionArrangement::makeFull(AudioBlockId block, int64_t sourceLength,
                                 std::vector<AudioRegion>& out,
                                 std::string* error) const {
    if (block == kInvalidAudioBlock || sourceLength <= 0) {
        setError(error, "audio region needs a block and a positive length");
        return false;
    }
    AudioRegion region;
    region.block = block;
    region.sourceLength = sourceLength;
    region.name = "Region 1";
    out = {std::move(region)};
    return true;
}

bool RegionArrangement::splitAt(int64_t frame,
                                std::vector<AudioRegion>& out,
                                std::string* error) const {
    out = m_regions;
    int index = -1;
    for (size_t i = 0; i < out.size(); ++i) {
        const int64_t end =
            out[i].timelineStart + regionOutputFrames(out[i]);
        if (frame > out[i].timelineStart && frame < end) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        setError(error, "split point is not inside an audio region");
        return false;
    }
    AudioRegion& region = out[static_cast<size_t>(index)];
    if (region.sourceLength < 2) {
        setError(error, "audio region is too short to split");
        return false;
    }
    const int64_t local = frame - region.timelineStart;
    const int64_t sourceOffset = std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(
            static_cast<double>(local) / region.stretch)),
        1, region.sourceLength - 1);

    AudioRegion left = region;
    left.sourceLength = sourceOffset;
    left.fadeOutSeconds = 0.0;
    AudioRegion right = region;
    right.sourceStart += sourceOffset;
    right.sourceLength -= sourceOffset;
    right.timelineStart = region.timelineStart + regionOutputFrames(left);
    right.fadeInSeconds = 0.0;
    if (!left.name.empty()) left.name += " A";
    if (!right.name.empty()) right.name += " B";
    out[static_cast<size_t>(index)] = std::move(left);
    out.insert(out.begin() + index + 1, std::move(right));
    return true;
}

bool RegionArrangement::splitAllAt(int64_t frame,
                                    std::vector<AudioRegion>& out,
                                    std::string* error) const {
    if (error) error->clear();
    if (frame < 0 || frame > durationFrames()) {
        setError(error, "split point is outside the arrangement");
        return false;
    }
    std::vector<AudioRegion> result;
    result.reserve(m_regions.size() + 1);
    for (const auto& region : m_regions) {
        const int64_t end =
            region.timelineStart + regionOutputFrames(region);
        if (frame <= region.timelineStart || frame >= end) {
            result.push_back(region);
            continue;
        }
        if (region.sourceLength < 2) {
            setError(error, "audio region is too short to split");
            return false;
        }
        const int64_t local = frame - region.timelineStart;
        const int64_t sourceOffset = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                static_cast<double>(local) / region.stretch)),
            1, region.sourceLength - 1);
        AudioRegion left = region;
        left.sourceLength = sourceOffset;
        left.fadeOutSeconds = 0.0;
        AudioRegion right = region;
        right.sourceStart += sourceOffset;
        right.sourceLength -= sourceOffset;
        right.timelineStart = region.timelineStart + regionOutputFrames(left);
        right.fadeInSeconds = 0.0;
        if (!left.name.empty()) left.name += " A";
        if (!right.name.empty()) right.name += " B";
        result.push_back(std::move(left));
        result.push_back(std::move(right));
    }
    out = std::move(result);
    return true;
}

bool RegionArrangement::eraseRange(int64_t start, int64_t end,
                                   std::vector<AudioRegion>& out,
                                   std::string* error) const {
    if (error) error->clear();
    const int64_t duration = durationFrames();
    end = std::min(end, duration);
    start = std::clamp<int64_t>(start, 0, duration);
    if (start >= end) {
        setError(error, "erase range is empty");
        return false;
    }
    if (!splitAllAt(start, out, error)) return false;
    std::vector<AudioRegion> split = std::move(out);
    RegionArrangement afterStart;
    afterStart.assign(split);
    if (!afterStart.splitAllAt(end, split, error)) return false;
    const int64_t removed = end - start;
    std::vector<AudioRegion> result;
    result.reserve(split.size());
    for (auto region : split) {
        const int64_t regionStart = region.timelineStart;
        const int64_t regionEnd =
            regionStart + regionOutputFrames(region);
        if (regionEnd <= start) {
            result.push_back(std::move(region));
        } else if (regionStart >= end) {
            region.timelineStart -= removed;
            result.push_back(std::move(region));
        } else if (regionStart >= start && regionEnd <= end) {
            continue;
        } else {
            setError(error, "erase range does not align with audio regions");
            return false;
        }
    }
    out = std::move(result);
    return true;
}

bool RegionArrangement::insertRegion(int64_t timelineStart,
                                     AudioBlockId block,
                                     int64_t sourceLength,
                                     std::vector<AudioRegion>& out,
                                     std::string* error) const {
    if (error) error->clear();
    if (timelineStart < 0 || timelineStart > durationFrames()) {
        setError(error, "insert position is outside the arrangement");
        return false;
    }
    if (block == kInvalidAudioBlock || sourceLength <= 0) {
        setError(error, "inserted audio region needs a positive source length");
        return false;
    }
    std::vector<AudioRegion> split;
    if (!splitAllAt(timelineStart, split, error)) return false;
    AudioRegion inserted;
    inserted.block = block;
    inserted.sourceLength = sourceLength;
    inserted.timelineStart = timelineStart;
    inserted.name = "Inserted region";
    const int64_t added = regionOutputFrames(inserted);
    if (added <= 0) {
        setError(error, "inserted audio region has an invalid length");
        return false;
    }

    std::vector<AudioRegion> result;
    result.reserve(split.size() + 1);
    bool placed = false;
    for (auto region : split) {
        if (!placed && region.timelineStart >= timelineStart) {
            result.push_back(inserted);
            placed = true;
        }
        if (region.timelineStart >= timelineStart)
            region.timelineStart += added;
        result.push_back(std::move(region));
    }
    if (!placed) result.push_back(std::move(inserted));
    out = std::move(result);
    return true;
}

bool RegionArrangement::replaceRange(int64_t start, int64_t end,
                                     AudioBlockId block,
                                     int64_t sourceLength,
                                     std::vector<AudioRegion>& out,
                                     std::string* error) const {
    if (error) error->clear();
    const int64_t duration = durationFrames();
    end = std::min(end, duration);
    start = std::clamp<int64_t>(start, 0, duration);
    if (start >= end) {
        setError(error, "replace range is empty");
        return false;
    }
    if (block == kInvalidAudioBlock || sourceLength <= 0) {
        setError(error, "replacement audio region needs a positive source length");
        return false;
    }
    std::vector<AudioRegion> split;
    if (!splitAllAt(start, split, error)) return false;
    RegionArrangement afterStart;
    afterStart.assign(split);
    if (!afterStart.splitAllAt(end, split, error)) return false;

    AudioRegion replacement;
    replacement.block = block;
    replacement.sourceLength = sourceLength;
    replacement.timelineStart = start;
    replacement.name = "Replaced region";
    const int64_t added = regionOutputFrames(replacement);
    if (added <= 0) {
        setError(error, "replacement audio region has an invalid length");
        return false;
    }
    const int64_t delta = added - (end - start);

    std::vector<AudioRegion> result;
    result.reserve(split.size() + 1);
    bool placed = false;
    for (auto region : split) {
        const int64_t regionStart = region.timelineStart;
        const int64_t regionEnd =
            regionStart + regionOutputFrames(region);
        if (regionEnd <= start) {
            result.push_back(std::move(region));
        } else if (regionStart >= end) {
            if (!placed) {
                result.push_back(std::move(replacement));
                placed = true;
            }
            region.timelineStart += delta;
            result.push_back(std::move(region));
        } else if (regionStart >= start && regionEnd <= end) {
            continue;
        } else {
            setError(error, "replace range does not align with audio regions");
            return false;
        }
    }
    if (!placed) result.push_back(std::move(replacement));
    out = std::move(result);
    return true;
}

bool RegionArrangement::cropRange(int64_t start, int64_t end,
                                  std::vector<AudioRegion>& out,
                                  std::string* error) const {
    if (error) error->clear();
    const int64_t duration = durationFrames();
    start = std::clamp<int64_t>(start, 0, duration);
    end = std::clamp<int64_t>(end, 0, duration);
    if (start >= end) {
        setError(error, "crop range is empty");
        return false;
    }
    std::vector<AudioRegion> split;
    if (!splitAllAt(start, split, error)) return false;
    RegionArrangement afterStart;
    afterStart.assign(split);
    if (!afterStart.splitAllAt(end, split, error)) return false;
    std::vector<AudioRegion> result;
    result.reserve(split.size());
    for (auto region : split) {
        const int64_t regionStart = region.timelineStart;
        const int64_t regionEnd =
            regionStart + regionOutputFrames(region);
        if (regionStart >= start && regionEnd <= end) {
            region.timelineStart -= start;
            result.push_back(std::move(region));
        } else if (regionEnd <= start || regionStart >= end) {
            continue;
        } else {
            setError(error, "crop range does not align with audio regions");
            return false;
        }
    }
    out = std::move(result);
    return true;
}

bool RegionArrangement::remove(size_t index,
                               std::vector<AudioRegion>& out,
                               std::string* error) const {
    if (index >= m_regions.size()) {
        setError(error, "audio region index is out of range");
        return false;
    }
    out = m_regions;
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool RegionArrangement::move(size_t index, int64_t timelineStart,
                             std::vector<AudioRegion>& out,
                             std::string* error) const {
    if (index >= m_regions.size()) {
        setError(error, "audio region index is out of range");
        return false;
    }
    if (timelineStart < 0) {
        setError(error, "audio region position must be non-negative");
        return false;
    }
    out = m_regions;
    out[index].timelineStart = timelineStart;
    return true;
}

bool RegionArrangement::stretch(size_t index, double ratio,
                                std::vector<AudioRegion>& out,
                                std::string* error) const {
    if (index >= m_regions.size()) {
        setError(error, "audio region index is out of range");
        return false;
    }
    if (!std::isfinite(ratio) || ratio <= 0.0) {
        setError(error, "audio region stretch ratio must be positive");
        return false;
    }
    out = m_regions;
    out[index].stretch = std::clamp(ratio, 0.05, 20.0);
    return true;
}

bool RegionArrangement::repeat(size_t index, int count,
                               std::vector<AudioRegion>& out,
                               std::string* error) const {
    if (index >= m_regions.size()) {
        setError(error, "audio region index is out of range");
        return false;
    }
    if (count <= 0 || count > 1000) {
        setError(error, "audio region repeat count must be between 1 and 1000");
        return false;
    }
    const AudioRegion& source = m_regions[index];
    const int64_t length = regionOutputFrames(source);
    const int64_t end = source.timelineStart + length;
    if (length > 0 &&
        count > std::numeric_limits<int64_t>::max() / length) {
        setError(error, "repeated audio region is too large");
        return false;
    }
    const int64_t added = length * static_cast<int64_t>(count);
    out.clear();
    out.reserve(m_regions.size() + static_cast<size_t>(count));
    for (size_t i = 0; i < m_regions.size(); ++i) {
        AudioRegion region = m_regions[i];
        if (i == index) {
            out.push_back(region);
            for (int repeat = 1; repeat <= count; ++repeat) {
                AudioRegion copy = region;
                copy.timelineStart =
                    region.timelineStart +
                    static_cast<int64_t>(repeat) * length;
                if (!copy.name.empty())
                    copy.name += " copy " + std::to_string(repeat);
                out.push_back(std::move(copy));
            }
        } else {
            if (region.timelineStart >= end) region.timelineStart += added;
            out.push_back(std::move(region));
        }
    }
    return true;
}

bool RegionArrangement::setGain(size_t index, double gainDb,
                                std::vector<AudioRegion>& out,
                                std::string* error) const {
    if (index >= m_regions.size()) {
        setError(error, "audio region index is out of range");
        return false;
    }
    if (!std::isfinite(gainDb)) {
        setError(error, "audio region gain must be finite");
        return false;
    }
    out = m_regions;
    out[index].gainDb = std::clamp(gainDb, -96.0, 24.0);
    return true;
}

bool RegionArrangement::setFades(size_t index, double fadeInSeconds,
                                 double fadeOutSeconds,
                                 std::vector<AudioRegion>& out,
                                 std::string* error) const {
    if (index >= m_regions.size()) {
        setError(error, "audio region index is out of range");
        return false;
    }
    if (!std::isfinite(fadeInSeconds) || !std::isfinite(fadeOutSeconds) ||
        fadeInSeconds < 0.0 || fadeOutSeconds < 0.0) {
        setError(error, "audio region fades must be non-negative");
        return false;
    }
    out = m_regions;
    out[index].fadeInSeconds = fadeInSeconds;
    out[index].fadeOutSeconds = fadeOutSeconds;
    return true;
}

} // namespace vwave
