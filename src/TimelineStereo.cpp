#include "vwave/Timeline.h"

#include "TimelineInternal.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace vwave::detail;

namespace vwave {

bool Timeline::splitStereoTrack(int index, bool keepOriginal,
                                std::string* error) {
    if (error) error->clear();
    if (index < 0 || index >= trackCount()) {
        setError(error, "track index is out of range");
        return false;
    }
    const TimelineTrack& source = m_tracks[static_cast<size_t>(index)];
    if (source.empty || source.document.audio().empty()) {
        setError(error, "track is empty");
        return false;
    }
    const Audio& audio = source.document.audio();
    if (audio.numChannels() != 2) {
        setError(error, "track is not stereo");
        return false;
    }

    Audio leftAudio(1, audio.numFrames(), audio.sampleRate());
    Audio rightAudio(1, audio.numFrames(), audio.sampleRate());
    for (int64_t frame = 0; frame < audio.numFrames(); ++frame) {
        leftAudio.sample(0, frame) = audio.sample(0, frame);
        rightAudio.sample(0, frame) = audio.sample(1, frame);
    }

    double leftBalance = 1.0;
    double rightBalance = 1.0;
    if (source.pan < 0.0)
        rightBalance = 1.0 + source.pan;
    else if (source.pan > 0.0)
        leftBalance = 1.0 - source.pan;

    TimelineTrack left;
    left.name = source.name + " L";
    left.document = Document::fromAudio(std::move(leftAudio));
    left.startFrame = source.startFrame;
    left.gainDb = std::clamp(
        source.gainDb + 20.0 * std::log10(std::max(leftBalance, 1e-6)),
        -96.0, 24.0);
    left.pan = -1.0;
    left.muted = source.muted || leftBalance <= 0.0;
    left.solo = source.solo;
    left.empty = false;

    TimelineTrack right;
    right.name = source.name + " R";
    right.document = Document::fromAudio(std::move(rightAudio));
    right.startFrame = source.startFrame;
    right.gainDb = std::clamp(
        source.gainDb + 20.0 * std::log10(std::max(rightBalance, 1e-6)),
        -96.0, 24.0);
    right.pan = 1.0;
    right.muted = source.muted || rightBalance <= 0.0;
    right.solo = source.solo;
    right.empty = false;

    if (keepOriginal)
        m_tracks[static_cast<size_t>(index)].muted = true;
    auto position = m_tracks.begin() + index + 1;
    position = m_tracks.insert(position, std::move(left));
    m_tracks.insert(position + 1, std::move(right));

    if (keepOriginal) {
        m_active = index + 1;
    } else {
        m_tracks.erase(m_tracks.begin() + index);
        m_active = index;
    }
    syncSelectionToTracks();
    return true;
}

bool Timeline::combineMonoTracks(int leftIndex, int rightIndex,
                                 StereoCombineOptions options,
                                 std::string* error) {
    if (error) error->clear();
    if (leftIndex < 0 || leftIndex >= trackCount() || rightIndex < 0 ||
        rightIndex >= trackCount()) {
        setError(error, "track index is out of range");
        return false;
    }
    if (leftIndex == rightIndex) {
        setError(error, "select two different tracks");
        return false;
    }
    const TimelineTrack& left = m_tracks[static_cast<size_t>(leftIndex)];
    const TimelineTrack& right = m_tracks[static_cast<size_t>(rightIndex)];
    if (left.empty || right.empty || left.document.audio().empty() ||
        right.document.audio().empty()) {
        setError(error, "both tracks must contain audio");
        return false;
    }
    const Audio& leftAudio = left.document.audio();
    const Audio& rightAudio = right.document.audio();
    if (leftAudio.numChannels() != 1 || rightAudio.numChannels() != 1) {
        setError(error, "both tracks must be mono");
        return false;
    }
    if (left.document.sampleRate() != m_sampleRate ||
        right.document.sampleRate() != m_sampleRate) {
        setError(error, "track sample rate does not match the project");
        return false;
    }

    const int64_t start = std::min(left.startFrame, right.startFrame);
    const int64_t targetFrames =
        std::max(left.startFrame + leftAudio.numFrames(),
                 right.startFrame + rightAudio.numFrames()) -
        start;
    if (targetFrames <= 0) {
        setError(error, "combined track is empty");
        return false;
    }

    Audio combined(2, targetFrames, m_sampleRate);
    auto addChannel = [&](int outChannel, const TimelineTrack& track,
                          const Audio& audio) {
        const int64_t offset = track.startFrame - start;
        const int64_t gap = targetFrames - offset - audio.numFrames();
        int64_t front = 0;
        int64_t back = 0;
        switch (options.padding) {
        case StereoPadding::Back:
            back = gap;
            break;
        case StereoPadding::Front:
            front = gap;
            break;
        case StereoPadding::Both:
            front = gap / 2;
            back = gap - front;
            break;
        }
        const int64_t destination = offset + front;
        const double gain = std::pow(10.0, track.gainDb / 20.0);
        for (int64_t frame = 0; frame < audio.numFrames(); ++frame) {
            combined.sample(outChannel, destination + frame) =
                static_cast<float>(audio.sample(0, frame) * gain);
        }
        (void)back;
    };

    addChannel(0, left, leftAudio);
    addChannel(1, right, rightAudio);

    TimelineTrack result;
    result.name = left.name + " + " + right.name;
    result.document = Document::fromAudio(std::move(combined));
    result.startFrame = start;
    result.gainDb = 0.0;
    result.pan = 0.0;
    result.empty = false;
    m_tracks.push_back(std::move(result));
    const int newIndex = trackCount() - 1;

    if (options.keepOriginals) {
        m_tracks[static_cast<size_t>(leftIndex)].muted = true;
        m_tracks[static_cast<size_t>(rightIndex)].muted = true;
        m_active = newIndex;
    } else {
        const int first = std::min(leftIndex, rightIndex);
        const int second = std::max(leftIndex, rightIndex);
        m_tracks.erase(m_tracks.begin() + second);
        m_tracks.erase(m_tracks.begin() + first);
        m_active = newIndex - 2;
    }
    syncSelectionToTracks();
    return true;
}

const char* stereoPaddingName(StereoPadding padding) {
    switch (padding) {
    case StereoPadding::Back: return "back";
    case StereoPadding::Front: return "front";
    case StereoPadding::Both: return "both";
    }
    return "back";
}

bool stereoPaddingFromName(const std::string& name, StereoPadding& padding) {
    if (name == "back" || name == "end")
        padding = StereoPadding::Back;
    else if (name == "front" || name == "start")
        padding = StereoPadding::Front;
    else if (name == "both" || name == "center" || name == "centre")
        padding = StereoPadding::Both;
    else
        return false;
    return true;
}

} // namespace vwave
