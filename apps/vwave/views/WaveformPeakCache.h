#pragma once

#include "vwave/Audio.h"
#include "vwave/UiModel.h"

#include <cstdint>
#include <vector>

namespace vwave::app {

class WaveformPeakCache {
public:
    const std::vector<vwave::PeakBin>& get(
        int trackIndex, const vwave::Audio& audio, uint64_t revision,
        int channel, vwave::Range range, int width) {
        for (auto& entry : m_entries) {
            if (entry.trackIndex != trackIndex || entry.channel != channel)
                continue;
            if (entry.audio == &audio && entry.revision == revision &&
                entry.range.start == range.start &&
                entry.range.end == range.end && entry.width == width)
                return entry.peaks;
            entry.audio = &audio;
            entry.revision = revision;
            entry.range = range;
            entry.width = width;
            entry.peaks = vwave::computePeaks(audio, channel, range, width);
            return entry.peaks;
        }
        Entry entry;
        entry.trackIndex = trackIndex;
        entry.channel = channel;
        entry.audio = &audio;
        entry.revision = revision;
        entry.range = range;
        entry.width = width;
        entry.peaks = vwave::computePeaks(audio, channel, range, width);
        m_entries.push_back(std::move(entry));
        return m_entries.back().peaks;
    }

private:
    struct Entry {
        int trackIndex = -1;
        int channel = -1;
        const vwave::Audio* audio = nullptr;
        uint64_t revision = 0;
        vwave::Range range;
        int width = 0;
        std::vector<vwave::PeakBin> peaks;
    };

    std::vector<Entry> m_entries;
};

} // namespace vwave::app
