#include "vwave/Timeline.h"

#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::string trackName(int index) {
    return "Track " + std::to_string(index + 1);
}

nlohmann::json selectionJson(const Selection& selection) {
    return {{"start", selection.start},
            {"end", selection.end},
            {"active", selection.active}};
}

Selection selectionFromJson(const nlohmann::json& json) {
    Selection selection;
    if (!json.is_object()) return selection;
    selection.start = json.value("start", int64_t{0});
    selection.end = json.value("end", int64_t{0});
    selection.active = json.value("active", false);
    return selection;
}

} // namespace

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

nlohmann::json Timeline::toJson() const {
    nlohmann::json root;
    root["format"] = "vwave-timeline";
    root["version"] = 1;
    root["sampleRate"] = m_sampleRate;
    root["activeTrack"] = m_active;
    root["selectionScope"] =
        m_selectionScope == SelectionScope::ActiveTrack ? "active_track"
                                                         : "all_tracks";
    root["selection"] = selectionJson(m_selection);
    root["tracks"] = nlohmann::json::array();
    for (const auto& item : m_tracks) {
        nlohmann::json trackJson;
        trackJson["name"] = item.name;
        if (!item.recipe.is_null() && !item.recipe.empty())
            trackJson["recipe"] = item.recipe;
        trackJson["startFrame"] = item.startFrame;
        trackJson["gainDb"] = item.gainDb;
        trackJson["pan"] = item.pan;
        trackJson["muted"] = item.muted;
        trackJson["solo"] = item.solo;
        trackJson["armed"] = item.armed;
        trackJson["empty"] = item.empty;
        if (!item.empty)
            trackJson["document"] = item.document.toJson();
        else
            trackJson["sampleRate"] = item.document.sampleRate();
        root["tracks"].push_back(std::move(trackJson));
    }
    return root;
}

bool Timeline::saveProject(const std::string& path,
                           std::string* error) const {
    if (error) error->clear();
    if (m_tracks.empty()) {
        setError(error, "project has no tracks");
        return false;
    }
    try {
        const nlohmann::json root = toJson();
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            setError(error, "cannot create project file");
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            setError(error, "failed while writing project file");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot save project: ") + e.what());
        return false;
    }
}

bool Timeline::loadProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            setError(error, "cannot open project file");
            return false;
        }
        nlohmann::json root;
        in >> root;

        if (root.value("format", std::string{}) == "vwave-project") {
            Document document;
            if (!Document::fromJson(root, document, error)) return false;
            Timeline timeline;
            std::string name = "Track 1";
            if (!document.sourcePath().empty())
                name = std::filesystem::path(document.sourcePath())
                           .stem()
                           .string();
            const Selection savedSelection = document.selection();
            if (!timeline.addTrack(name, std::move(document), 0, error))
                return false;
            timeline.setSelection(savedSelection.start, savedSelection.end);
            *this = std::move(timeline);
            return true;
        }

        if (root.value("format", std::string{}) != "vwave-timeline") {
            setError(error, "not a VWaveEditor project");
            return false;
        }
        if (root.value("version", 0) != 1) {
            setError(error, "unsupported project version");
            return false;
        }
        const int sampleRate = root.value("sampleRate", 0);
        if (sampleRate <= 0) {
            setError(error, "project has an invalid sample rate");
            return false;
        }
        if (!root.contains("tracks") || !root["tracks"].is_array() ||
            root["tracks"].empty()) {
            setError(error, "project has no tracks");
            return false;
        }

        Timeline loaded;
        loaded.m_sampleRate = sampleRate;
        for (const auto& trackJson : root["tracks"]) {
            TimelineTrack item;
            item.name = trackJson.value("name", std::string{});
            if (trackJson.contains("recipe") &&
                trackJson["recipe"].is_object())
                item.recipe = trackJson["recipe"];
            item.startFrame = trackJson.value("startFrame", int64_t{0});
            item.gainDb = trackJson.value("gainDb", 0.0);
            item.pan = trackJson.value("pan", 0.0);
            item.muted = trackJson.value("muted", false);
            item.solo = trackJson.value("solo", false);
            item.armed = trackJson.value("armed", false);
            item.empty = trackJson.value("empty", false);
            if (item.startFrame < 0 || !std::isfinite(item.gainDb) ||
                !std::isfinite(item.pan)) {
                setError(error, "invalid track metadata");
                return false;
            }
            item.gainDb = std::clamp(item.gainDb, -96.0, 24.0);
            item.pan = std::clamp(item.pan, -1.0, 1.0);
            if (item.empty) {
                const int trackRate =
                    trackJson.value("sampleRate", sampleRate);
                item.document = Document::fromAudio(
                    Audio(1, 0, trackRate));
                item.empty = true;
            } else {
                if (!trackJson.contains("document")) {
                    setError(error, "track is missing its document");
                    return false;
                }
                if (!Document::fromJson(trackJson["document"],
                                        item.document, error))
                    return false;
                if (item.document.sampleRate() != sampleRate) {
                    setError(error, "track sample rate does not match project");
                    return false;
                }
                item.empty = item.document.source().empty();
            }
            loaded.m_tracks.push_back(std::move(item));
        }

        loaded.m_active = root.value("activeTrack", 0);
        if (loaded.m_tracks.empty()) loaded.m_active = -1;
        else loaded.m_active = std::clamp(
            loaded.m_active, 0, static_cast<int>(loaded.m_tracks.size()) - 1);
        loaded.m_selectionScope =
            root.value("selectionScope", std::string{"all_tracks"}) ==
                    "active_track"
                ? SelectionScope::ActiveTrack
                : SelectionScope::AllTracks;
        loaded.m_selection = selectionFromJson(
            root.value("selection", nlohmann::json{}));
        loaded.setSelection(loaded.m_selection.start, loaded.m_selection.end);
        *this = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

} // namespace vwave
