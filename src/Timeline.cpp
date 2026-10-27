#include "vwave/Timeline.h"

#include "TimelineInternal.h"

#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace vwave {

using namespace detail;

int64_t Timeline::lengthFrames() const {
    int64_t frames = 0;
    for (const auto& item : m_tracks) {
        if (item.document.audio().empty()) continue;
        frames = std::max(frames,
                          item.startFrame + item.document.numFrames());
    }
    return frames;
}

double Timeline::duration() const {
    return m_sampleRate > 0
               ? static_cast<double>(lengthFrames()) / m_sampleRate
               : 0.0;
}

void Timeline::setActiveTrack(int index) {
    if (m_tracks.empty()) {
        m_active = -1;
        return;
    }
    m_active = std::clamp(index, 0, static_cast<int>(m_tracks.size()) - 1);
    if (m_selectionScope == SelectionScope::ActiveTrack)
        syncSelectionToTracks();
}

void Timeline::setSelectionScope(SelectionScope scope) {
    m_selectionScope = scope;
    syncSelectionToTracks();
}

TimelineTrack& Timeline::track(int index) {
    return m_tracks.at(static_cast<size_t>(index));
}

const TimelineTrack& Timeline::track(int index) const {
    return m_tracks.at(static_cast<size_t>(index));
}

void Timeline::setSelection(int64_t start, int64_t end) {
    const int64_t total = lengthFrames();
    const int64_t clampedStart = std::clamp<int64_t>(start, 0, total);
    const int64_t clampedEnd = std::clamp<int64_t>(end, 0, total);
    if (clampedEnd <= clampedStart) {
        m_selection.clear();
    } else {
        m_selection = {clampedStart, clampedEnd, true};
    }
    syncSelectionToTracks();
}

void Timeline::clearSelection() {
    m_selection.clear();
    syncSelectionToTracks();
}

Range Timeline::selectedRange() const {
    if (!m_selection.active) return {};
    return m_selection.range();
}

Range Timeline::activeRange() const {
    if (m_selection.active) return m_selection.range();
    return {0, lengthFrames()};
}

void Timeline::syncSelectionToTracks() {
    for (int index = 0; index < trackCount(); ++index) {
        auto& item = m_tracks[static_cast<size_t>(index)];
        if (!m_selection.active ||
            (m_selectionScope == SelectionScope::ActiveTrack &&
             index != m_active)) {
            item.document.clearSelection();
            continue;
        }
        const int64_t localStart = m_selection.start - item.startFrame;
        const int64_t localEnd = m_selection.end - item.startFrame;
        item.document.setSelection(localStart, localEnd);
    }
}

bool Timeline::cropTrack(int index, Range range, std::string* error) {
    if (error) error->clear();
    if (index < 0 || index >= trackCount()) {
        setError(error, "track index is out of range");
        return false;
    }
    if (range.length() <= 0) {
        if (range.start != 0 || range.end != 0) {
            setError(error, "invalid crop range");
            return false;
        }
        range = selectedRange();
    }
    if (range.length() <= 0) {
        setError(error, "no active selection");
        return false;
    }
    auto& item = m_tracks[static_cast<size_t>(index)];
    const int64_t frames = item.document.numFrames();
    if (frames <= 0) {
        setError(error, "track is empty");
        return false;
    }
    const int64_t trackStart = item.startFrame;
    const int64_t trackEnd = trackStart + frames;
    const int64_t globalStart = std::max(range.start, trackStart);
    const int64_t globalEnd = std::min(range.end, trackEnd);
    if (globalEnd <= globalStart) {
        setError(error, "selection does not intersect the track");
        return false;
    }
    const int64_t localStart = globalStart - trackStart;
    const int64_t localEnd = globalEnd - trackStart;
    item.document.setSelection(localStart, localEnd);
    if (!item.document.cropToSelection(error)) return false;
    item.startFrame = globalStart;
    item.recipe = {};
    m_selection.clear();
    syncSelectionToTracks();
    return true;
}

bool Timeline::shiftTrack(int index, int64_t deltaFrames,
                          std::string* error) {
    if (error) error->clear();
    if (index < 0 || index >= trackCount()) {
        setError(error, "track index is out of range");
        return false;
    }
    auto& item = m_tracks[static_cast<size_t>(index)];
    const int64_t shifted = item.startFrame + deltaFrames;
    if (shifted < 0) {
        setError(error, "track cannot start before zero");
        return false;
    }
    item.startFrame = shifted;
    syncSelectionToTracks();
    return true;
}

} // namespace vwave
