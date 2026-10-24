#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace vwave {

struct Range {
    int64_t start = 0;
    int64_t end = 0;

    int64_t length() const { return end > start ? end - start : 0; }
    bool empty() const { return length() == 0; }

    Range clamped(int64_t limit) const {
        const int64_t s = std::clamp<int64_t>(start, 0, limit);
        const int64_t e = std::clamp<int64_t>(end, 0, limit);
        return e > s ? Range{s, e} : Range{s, s};
    }

    static Range fromLength(int64_t start, int64_t length) {
        return {start, start + std::max<int64_t>(0, length)};
    }
};

struct Selection {
    int64_t start = 0;
    int64_t end = 0;
    bool active = false;

    int64_t length() const { return end > start ? end - start : 0; }
    bool empty() const { return !active || length() == 0; }
    Range range() const { return {start, end}; }
    void clear() {
        start = 0;
        end = 0;
        active = false;
    }
};

class Audio {
public:
    Audio() = default;

    Audio(int numChannels, int64_t numFrames, int sampleRate = 48000)
        : m_channels(numChannels), m_frames(numFrames), m_sampleRate(sampleRate) {
        if (m_channels < 0 || m_frames < 0 || m_sampleRate < 0)
            throw std::invalid_argument("Audio dimensions must be non-negative");
        m_data.assign(static_cast<size_t>(m_channels) *
                          static_cast<size_t>(m_frames),
                      0.0f);
    }

    int numChannels() const { return m_channels; }
    int64_t numFrames() const { return m_frames; }
    int sampleRate() const { return m_sampleRate; }
    bool empty() const { return m_frames <= 0 || m_channels <= 0; }
    double duration() const {
        return m_sampleRate > 0
                   ? static_cast<double>(m_frames) / m_sampleRate
                   : 0.0;
    }

    float* data() { return m_data.data(); }
    const float* data() const { return m_data.data(); }
    size_t totalSamples() const { return m_data.size(); }

    float* channel(int channel) {
        assert(channel >= 0 && channel < m_channels);
        return m_data.data() + static_cast<size_t>(channel) *
                                   static_cast<size_t>(m_frames);
    }

    const float* channel(int channel) const {
        assert(channel >= 0 && channel < m_channels);
        return m_data.data() + static_cast<size_t>(channel) *
                                   static_cast<size_t>(m_frames);
    }

    float& sample(int channel, int64_t frame) {
        assert(channel >= 0 && channel < m_channels);
        assert(frame >= 0 && frame < m_frames);
        return m_data[static_cast<size_t>(channel) *
                          static_cast<size_t>(m_frames) +
                      static_cast<size_t>(frame)];
    }

    float sample(int channel, int64_t frame) const {
        assert(channel >= 0 && channel < m_channels);
        assert(frame >= 0 && frame < m_frames);
        return m_data[static_cast<size_t>(channel) *
                          static_cast<size_t>(m_frames) +
                      static_cast<size_t>(frame)];
    }

    void resize(int numChannels, int64_t numFrames, int sampleRate) {
        if (numChannels < 0 || numFrames < 0 || sampleRate < 0)
            throw std::invalid_argument("Audio dimensions must be non-negative");
        m_channels = numChannels;
        m_frames = numFrames;
        m_sampleRate = sampleRate;
        m_data.assign(static_cast<size_t>(m_channels) *
                          static_cast<size_t>(m_frames),
                      0.0f);
    }

    void clear() { std::fill(m_data.begin(), m_data.end(), 0.0f); }

    Audio slice(Range range) const {
        const Range r = range.clamped(m_frames);
        Audio out(m_channels, r.length(), m_sampleRate);
        for (int ch = 0; ch < m_channels; ++ch) {
            std::copy_n(channel(ch) + r.start, static_cast<size_t>(r.length()),
                        out.channel(ch));
        }
        return out;
    }

    std::vector<float> toInterleaved() const {
        std::vector<float> out(static_cast<size_t>(m_frames) *
                               static_cast<size_t>(m_channels),
                               0.0f);
        for (int64_t frame = 0; frame < m_frames; ++frame) {
            for (int ch = 0; ch < m_channels; ++ch) {
                out[static_cast<size_t>(frame) *
                        static_cast<size_t>(m_channels) +
                    static_cast<size_t>(ch)] = sample(ch, frame);
            }
        }
        return out;
    }

    static Audio fromInterleaved(const float* data, int numChannels,
                                 int64_t numFrames, int sampleRate) {
        if (data == nullptr && numChannels > 0 && numFrames > 0)
            throw std::invalid_argument("Interleaved data must not be null");
        Audio out(numChannels, numFrames, sampleRate);
        for (int64_t frame = 0; frame < numFrames; ++frame) {
            for (int ch = 0; ch < numChannels; ++ch) {
                out.sample(ch, frame) =
                    data[static_cast<size_t>(frame) *
                            static_cast<size_t>(numChannels) +
                        static_cast<size_t>(ch)];
            }
        }
        return out;
    }

    static Audio silence(int numChannels, int64_t numFrames, int sampleRate) {
        return Audio(numChannels, numFrames, sampleRate);
    }

    float peak() const {
        float maxValue = 0.0f;
        for (float value : m_data) {
            const float magnitude = std::abs(value);
            if (magnitude > maxValue) maxValue = magnitude;
        }
        return maxValue;
    }

    float rms() const {
        if (m_data.empty()) return 0.0f;
        double sum = 0.0;
        for (float value : m_data) sum += static_cast<double>(value) * value;
        return static_cast<float>(std::sqrt(sum / static_cast<double>(m_data.size())));
    }

    bool equals(const Audio& other) const {
        if (m_channels != other.m_channels || m_frames != other.m_frames ||
            m_sampleRate != other.m_sampleRate)
            return false;
        if (m_data.size() != other.m_data.size()) return false;
        for (size_t i = 0; i < m_data.size(); ++i) {
            if (m_data[i] != other.m_data[i]) return false;
        }
        return true;
    }

private:
    int m_channels = 0;
    int64_t m_frames = 0;
    int m_sampleRate = 48000;
    std::vector<float> m_data;
};

} // namespace vwave
