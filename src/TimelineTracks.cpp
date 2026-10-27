#include "vwave/Timeline.h"

#include "TimelineInternal.h"

#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

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

} // namespace vwave
