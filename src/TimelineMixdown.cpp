#include "vwave/Timeline.h"

#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>

namespace vwave {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

bool Timeline::trackAudible(const TimelineTrack& item) const {
    if (item.muted) return false;
    if (anySolo() && !item.solo) return false;
    return true;
}

Audio Timeline::mixdown(Range range) const {
    const int64_t total = lengthFrames();
    if (total <= 0) return Audio{};
    if (range.length() <= 0) {
        if (range.start != 0 || range.end != 0) return Audio{};
        range = {0, total};
    }
    range = range.clamped(total);
    if (range.empty()) return Audio{};

    Audio output(2, range.length(), m_sampleRate);
    for (const auto& item : m_tracks) {
        if (!trackAudible(item)) continue;
        Audio source = item.document.audio();
        if (source.empty()) continue;
        if (source.sampleRate() != m_sampleRate)
            source = resample(source, m_sampleRate);
        if (source.empty()) continue;

        const double gain = std::pow(10.0, item.gainDb / 20.0);
        double leftGain = 1.0;
        double rightGain = 1.0;
        if (source.numChannels() == 1) {
            const double angle = (item.pan + 1.0) * kPi / 4.0;
            leftGain = std::cos(angle);
            rightGain = std::sin(angle);
        } else if (item.pan < 0.0) {
            rightGain = 1.0 + item.pan;
        } else if (item.pan > 0.0) {
            leftGain = 1.0 - item.pan;
        }
        leftGain *= gain;
        rightGain *= gain;

        const int64_t sourceStart = std::max<int64_t>(range.start,
                                                      item.startFrame);
        const int64_t sourceEnd = std::min<int64_t>(
            range.end, item.startFrame + source.numFrames());
        for (int64_t frame = sourceStart; frame < sourceEnd; ++frame) {
            const int64_t index = frame - item.startFrame;
            const int64_t outputIndex = frame - range.start;
            const float left = source.sample(0, index);
            const float right = source.numChannels() > 1
                                    ? source.sample(1, index)
                                    : left;
            output.sample(0, outputIndex) +=
                static_cast<float>(left * leftGain);
            output.sample(1, outputIndex) +=
                static_cast<float>(right * rightGain);
        }
    }
    return output;
}

} // namespace vwave
