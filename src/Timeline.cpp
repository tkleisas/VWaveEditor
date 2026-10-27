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

bool Timeline::validateSampleRate(const Audio& audio,
                                  std::string* error) const {
    if (audio.empty()) {
        setError(error, "track audio is empty");
        return false;
    }
    if (m_tracks.empty() || m_sampleRate <= 0) return true;
    if (audio.sampleRate() != m_sampleRate) {
        setError(error, "track sample rate does not match the project");
        return false;
    }
    return true;
}

bool Timeline::addTrack(std::string name, Document document,
                        int64_t startFrame, std::string* error) {
    if (error) error->clear();
    if (document.source().empty()) {
        setError(error, "cannot add an empty document");
        return false;
    }
    if (startFrame < 0) {
        setError(error, "track start frame must be non-negative");
        return false;
    }
    const Audio& audio = document.audio();
    if (!validateSampleRate(audio, error)) return false;
    if (name.empty()) name = trackName(trackCount());

    if (m_tracks.empty()) m_sampleRate = document.sampleRate();
    TimelineTrack item;
    item.name = std::move(name);
    item.document = std::move(document);
    item.startFrame = startFrame;
    item.empty = false;
    m_tracks.push_back(std::move(item));
    if (m_active < 0) m_active = 0;
    syncSelectionToTracks();
    return true;
}

bool Timeline::addAudioTrack(std::string name, const Audio& audio,
                             int64_t startFrame, std::string* error) {
    if (error) error->clear();
    if (audio.empty()) {
        setError(error, "cannot add empty audio");
        return false;
    }
    if (startFrame < 0) {
        setError(error, "track start frame must be non-negative");
        return false;
    }
    if (m_tracks.empty()) m_sampleRate = audio.sampleRate();
    Audio converted = audio;
    if (audio.sampleRate() != m_sampleRate)
        converted = resample(audio, m_sampleRate);
    if (converted.empty()) {
        setError(error, "cannot resample track audio");
        return false;
    }
    return addTrack(std::move(name), Document::fromAudio(std::move(converted)),
                    startFrame, error);
}

bool Timeline::addEmptyTrack(std::string name, int64_t startFrame,
                             std::string* error) {
    if (error) error->clear();
    if (startFrame < 0) {
        setError(error, "track start frame must be non-negative");
        return false;
    }
    if (m_tracks.empty()) m_sampleRate = 48000;
    if (name.empty()) name = trackName(trackCount());
    TimelineTrack item;
    item.name = std::move(name);
    item.startFrame = startFrame;
    item.empty = true;
    item.document = Document::fromAudio(
        Audio(1, 0, m_sampleRate));
    m_tracks.push_back(std::move(item));
    if (m_active < 0) m_active = 0;
    syncSelectionToTracks();
    return true;
}

bool Timeline::setTrackAudio(int index, Audio audio, int64_t startFrame,
                             std::string* error) {
    if (error) error->clear();
    if (index < 0 || index >= trackCount()) {
        setError(error, "track index is out of range");
        return false;
    }
    if (audio.empty()) {
        setError(error, "cannot set empty track audio");
        return false;
    }
    if (startFrame < 0) {
        setError(error, "track start frame must be non-negative");
        return false;
    }
    Audio converted = audio;
    if (audio.sampleRate() != m_sampleRate)
        converted = resample(audio, m_sampleRate);
    if (converted.empty()) {
        setError(error, "cannot resample track audio");
        return false;
    }
    auto& item = m_tracks[static_cast<size_t>(index)];
    item.document = Document::fromAudio(std::move(converted));
    item.startFrame = startFrame;
    item.empty = false;
    syncSelectionToTracks();
    return true;
}

bool Timeline::removeTrack(int index) {
    if (index < 0 || index >= trackCount()) return false;
    m_tracks.erase(m_tracks.begin() + index);
    if (m_tracks.empty()) {
        m_active = -1;
        m_sampleRate = 0;
        m_selection.clear();
    } else if (m_active >= trackCount()) {
        m_active = trackCount() - 1;
    } else if (m_active > index) {
        --m_active;
    } else if (m_active == index) {
        m_active = std::min(index, trackCount() - 1);
    }
    if (lengthFrames() > 0)
        m_selection = Selection{std::clamp<int64_t>(m_selection.start, 0,
                                                    lengthFrames()),
                                std::clamp<int64_t>(m_selection.end, 0,
                                                    lengthFrames()),
                                m_selection.active};
    syncSelectionToTracks();
    return true;
}

bool Timeline::setTrackName(int index, std::string name) {
    if (index < 0 || index >= trackCount()) return false;
    if (name.empty()) name = trackName(index);
    m_tracks[static_cast<size_t>(index)].name = std::move(name);
    return true;
}

bool Timeline::setTrackGainDb(int index, double db) {
    if (index < 0 || index >= trackCount() || !std::isfinite(db)) return false;
    m_tracks[static_cast<size_t>(index)].gainDb = std::clamp(db, -96.0, 24.0);
    return true;
}

bool Timeline::setTrackPan(int index, double pan) {
    if (index < 0 || index >= trackCount() || !std::isfinite(pan)) return false;
    m_tracks[static_cast<size_t>(index)].pan = std::clamp(pan, -1.0, 1.0);
    return true;
}

bool Timeline::setTrackMuted(int index, bool muted) {
    if (index < 0 || index >= trackCount()) return false;
    m_tracks[static_cast<size_t>(index)].muted = muted;
    return true;
}

bool Timeline::setTrackSolo(int index, bool solo) {
    if (index < 0 || index >= trackCount()) return false;
    m_tracks[static_cast<size_t>(index)].solo = solo;
    return true;
}

bool Timeline::setTrackArmed(int index, bool armed) {
    if (index < 0 || index >= trackCount()) return false;
    m_tracks[static_cast<size_t>(index)].armed = armed;
    return true;
}

bool Timeline::setTrackStartFrame(int index, int64_t frame) {
    if (index < 0 || index >= trackCount() || frame < 0) return false;
    m_tracks[static_cast<size_t>(index)].startFrame = frame;
    return true;
}

bool Timeline::anySolo() const {
    return std::any_of(m_tracks.begin(), m_tracks.end(),
                       [](const TimelineTrack& item) { return item.solo; });
}

int Timeline::armedTrack() const {
    for (int i = 0; i < trackCount(); ++i) {
        if (m_tracks[static_cast<size_t>(i)].armed) return i;
    }
    return -1;
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
