#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <algorithm>
#include <limits>
#include <utility>

using namespace vwave::detail;

namespace vwave {

namespace {

Operation labelledOperation(Operation::Kind kind, std::string label) {
    Operation operation;
    operation.kind = kind;
    operation.label = std::move(label);
    return operation;
}

} // namespace

Document::Document() = default;

Document::Document(Audio source) : m_source(std::move(source)) {
    initializeSource();
}

Document Document::fromAudio(Audio source) {
    return Document(std::move(source));
}

bool Document::initializeSource() {
    m_store.clear();
    m_arrangement.clear();
    m_sourceBlock = kInvalidAudioBlock;
    m_arranged = Audio{};
    m_arrangedValid = false;
    m_clipView.clear();
    m_clipViewValid = false;
    m_liveBlockDirectory.clear();
    m_channels = m_source.numChannels();
    m_sampleRate = m_source.sampleRate();
    if (m_source.empty()) return true;
    m_sourceBlock = m_store.add(m_source, "Source");
    std::vector<AudioRegion> regions;
    if (!m_arrangement.makeFull(m_sourceBlock, m_source.numFrames(), regions))
        return false;
    m_arrangement.assign(std::move(regions));
    return true;
}

void Document::invalidateArranged() {
    m_arrangedValid = false;
    m_clipViewValid = false;
}

bool Document::currentFormat(int& channels, int& sampleRate) const {
    const auto& regions = m_arrangement.regions();
    if (regions.empty()) {
        channels = m_channels;
        sampleRate = m_sampleRate;
        return channels > 0 && sampleRate > 0;
    }
    const AudioBlockInfo info = m_store.info(regions.front().block);
    if (info.id == kInvalidAudioBlock) return false;
    channels = info.channels;
    sampleRate = info.sampleRate;
    return channels > 0 && sampleRate > 0;
}

const Audio& Document::audio() const {
    if (!m_arrangedValid) {
        if (m_arrangement.empty()) {
            m_arranged = Audio(m_channels, 0,
                               m_sampleRate > 0 ? m_sampleRate : 48000);
        } else {
            std::string error;
            m_arranged = m_arrangement.render(m_store, &error);
            if (!error.empty())
                m_arranged = Audio(m_channels, 0,
                                   m_sampleRate > 0 ? m_sampleRate : 48000);
        }
        m_arrangedValid = true;
    }
    return m_arranged;
}

int Document::sampleRate() const { return m_sampleRate; }

int Document::numChannels() const { return m_channels; }

int64_t Document::numFrames() const { return m_arrangement.durationFrames(); }

double Document::duration() const {
    return m_sampleRate > 0
               ? static_cast<double>(numFrames()) /
                     static_cast<double>(m_sampleRate)
               : 0.0;
}

void Document::setSelection(int64_t start, int64_t end) {
    Selection selection;
    selection.start = start;
    selection.end = end;
    selection.active = end > start;
    setSelection(selection);
}

void Document::setSelection(const Selection& selection) {
    m_selection = clampSelection(selection, numFrames());
}

void Document::selectAll() {
    const int64_t frames = numFrames();
    m_selection = {0, frames, frames > 0};
}

void Document::clearSelection() { m_selection.clear(); }

Range Document::selectedRange() const {
    if (!m_selection.active) return {};
    return m_selection.range();
}

Range Document::activeRange() const {
    if (m_selection.active) return m_selection.range();
    return {0, numFrames()};
}

bool Document::hasClips() const { return !m_arrangement.empty(); }

void Document::refreshClipView() const {
    m_clipView.clear();
    m_clipView.reserve(m_arrangement.regions().size());
    for (const auto& region : m_arrangement.regions()) {
        Clip clip;
        clip.sourceStart = region.sourceStart;
        clip.sourceLength = region.sourceLength;
        clip.timelineStart = region.timelineStart;
        clip.stretch = region.stretch;
        clip.gainDb = region.gainDb;
        clip.fadeInSeconds = region.fadeInSeconds;
        clip.fadeOutSeconds = region.fadeOutSeconds;
        clip.name = region.name;
        m_clipView.push_back(std::move(clip));
    }
    m_clipViewValid = true;
}

const std::vector<Clip>& Document::clips() const {
    if (!m_clipViewValid) refreshClipView();
    return m_clipView;
}

const std::vector<AudioRegion>& Document::regions() const {
    return m_arrangement.regions();
}

int64_t Document::regionDurationFrames() const {
    return m_arrangement.durationFrames();
}

int Document::clipIndexAt(int64_t frame) const {
    return m_arrangement.indexAt(frame);
}

bool Document::createClipArrangement(std::string* error) {
    if (m_source.empty() && m_arrangement.empty()) {
        setError(error, "document is empty");
        return false;
    }
    if (!m_arrangement.empty()) return true;
    if (!initializeSource()) {
        setError(error, "cannot create clip arrangement");
        return false;
    }
    m_arrangedValid = false;
    m_clipViewValid = false;
    ++m_revision;
    return true;
}

bool Document::regionsFromClips(const std::vector<Clip>& clips,
                                AudioBlockId block, int64_t baseFrames,
                                std::vector<AudioRegion>& out,
                                std::string* error) const {
    out.clear();
    out.reserve(clips.size());
    for (const auto& clip : clips) {
        if (clip.sourceStart < 0 || clip.sourceLength <= 0 ||
            clip.sourceStart >
                std::numeric_limits<int64_t>::max() - clip.sourceLength ||
            clip.sourceStart + clip.sourceLength > baseFrames) {
            setError(error, "clip source range is outside the document");
            return false;
        }
        if (clip.timelineStart < 0) {
            setError(error, "clip timeline position must be non-negative");
            return false;
        }
        if (clipOutputFrames(clip) <= 0 || !std::isfinite(clip.stretch) ||
            clip.stretch <= 0.0) {
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
        AudioRegion region;
        region.block = block;
        region.sourceStart = clip.sourceStart;
        region.sourceLength = clip.sourceLength;
        region.timelineStart = clip.timelineStart;
        region.stretch = clip.stretch;
        region.gainDb = clip.gainDb;
        region.fadeInSeconds = clip.fadeInSeconds;
        region.fadeOutSeconds = clip.fadeOutSeconds;
        region.name = clip.name;
        out.push_back(std::move(region));
    }
    return true;
}

bool Document::setClipArrangementInternal(std::vector<Clip> clips,
                                          const std::string& label,
                                          std::string* error) {
    if (clips.empty()) {
        setError(error, "clip arrangement is empty");
        return false;
    }
    const Audio base = audio();
    if (base.empty()) {
        setError(error, "document is empty");
        return false;
    }
    const AudioBlockId block = m_store.add(base, "Clip base");
    std::vector<AudioRegion> regions;
    if (!regionsFromClips(clips, block, base.numFrames(), regions, error))
        return false;
    Operation operation = labelledOperation(Operation::Kind::SetClips,
                                            label.empty() ? "Set regions"
                                                          : label);
    operation.params["clips"] = nlohmann::json::array();
    for (const auto& clip : clips)
        operation.params["clips"].push_back(clipToJson(clip));
    return commitRegions(std::move(operation), std::move(regions),
                         m_selection, m_selection, error);
}

bool Document::setClipArrangement(std::vector<Clip> clips, std::string label,
                                  std::string* error) {
    return setClipArrangementInternal(std::move(clips), std::move(label),
                                      error);
}

bool Document::splitClipAt(int64_t frame, std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.splitAt(frame, regions, error)) return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Split region"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::deleteClip(size_t index, std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.remove(index, regions, error)) return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Delete region"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::moveClip(size_t index, int64_t timelineStart,
                        std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.move(index, timelineStart, regions, error))
        return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Move region"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::stretchClip(size_t index, double ratio, std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.stretch(index, ratio, regions, error)) return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Stretch region"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::repeatClip(size_t index, int count, std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.repeat(index, count, regions, error)) return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Repeat region"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::setClipGain(size_t index, double gainDb, std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.setGain(index, gainDb, regions, error)) return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Set region gain"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::setClipFades(size_t index, double fadeInSeconds,
                            double fadeOutSeconds, std::string* error) {
    std::vector<AudioRegion> regions;
    if (!m_arrangement.setFades(index, fadeInSeconds, fadeOutSeconds,
                                regions, error))
        return false;
    return commitRegions(
        labelledOperation(Operation::Kind::SetClips, "Set region fades"),
        std::move(regions), m_selection, m_selection, error);
}

bool Document::flattenClipsInternal(const Operation& operation,
                                    std::string* error) {
    if (m_arrangement.empty()) {
        setError(error, "no clip arrangement to flatten");
        return false;
    }
    const Audio rendered = audio();
    if (rendered.empty()) {
        setError(error, "cannot flatten an empty clip arrangement");
        return false;
    }
    const AudioBlockId block = m_store.add(rendered, "Flattened");
    std::vector<AudioRegion> regions;
    RegionArrangement builder;
    if (!builder.makeFull(block, rendered.numFrames(), regions, error))
        return false;
    Operation op = operation;
    if (op.label.empty()) op.label = "Flatten regions";
    return commitRegions(std::move(op), std::move(regions), m_selection,
                         m_selection, error);
}

bool Document::flattenClips(std::string* error) {
    return flattenClipsInternal(
        labelledOperation(Operation::Kind::FlattenClips, "Flatten regions"),
        error);
}

bool Document::commitRegions(Operation operation,
                             std::vector<AudioRegion> regions,
                             Selection before, Selection after,
                             std::string* error) {
    if (error) error->clear();
    if (!m_arrangement.validate(regions, m_store, error)) return false;
    HistoryEntry entry;
    entry.operation = std::move(operation);
    entry.before = m_arrangement.regions();
    entry.after = std::move(regions);
    entry.beforeSelection = clampSelection(before, numFrames());
    entry.stamp = nextHistoryStampForNewAction();

    m_arrangement.assign(entry.after);
    invalidateArranged();
    if (!m_arrangement.empty()) {
        const AudioBlockInfo info =
            m_store.info(m_arrangement.regions().front().block);
        if (info.id != kInvalidAudioBlock) {
            m_channels = info.channels;
            m_sampleRate = info.sampleRate;
        }
    }
    ++m_revision;
    m_selection = clampSelection(after, m_arrangement.durationFrames());
    entry.afterSelection = m_selection;
    m_history.push_back(std::move(entry));
    m_redo.clear();
    if (m_history.size() > m_historyLimit) {
        const size_t drop = m_history.size() - m_historyLimit;
        m_history.erase(m_history.begin(),
                        m_history.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    m_store.retain(referencedBlockIds());
    if (!m_liveBlockDirectory.empty()) {
        std::string spillError;
        m_store.spill(m_liveBlockDirectory, referencedBlockIds(),
                      &spillError);
    }
    return true;
}

void Document::setHistoryLimit(size_t entries) {
    m_historyLimit = std::max<size_t>(1, entries);
    if (m_history.size() > m_historyLimit) {
        const size_t drop = m_history.size() - m_historyLimit;
        m_history.erase(m_history.begin(),
                        m_history.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    m_store.retain(referencedBlockIds());
}

void Document::clearHistory() {
    const Audio current = audio();
    const std::filesystem::path liveDirectory = m_liveBlockDirectory;
    m_history.clear();
    m_redo.clear();
    m_source = current;
    initializeSource();
    m_liveBlockDirectory = liveDirectory;
    m_selection = clampSelection(m_selection, numFrames());
    ++m_revision;
}

void Document::clearClipboard() { m_clipboard = Audio{}; }

bool Document::addMarker(int64_t frame, std::string name) {
    const int64_t frames = numFrames();
    if (frame < 0 || frame > frames) return false;
    m_markers.push_back({frame, std::move(name)});
    std::stable_sort(m_markers.begin(), m_markers.end(),
                     [](const Marker& a, const Marker& b) {
                         return a.frame < b.frame;
                     });
    return true;
}

bool Document::removeMarker(size_t index) {
    if (index >= m_markers.size()) return false;
    m_markers.erase(m_markers.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void Document::clearMarkers() { m_markers.clear(); }

void Document::setSlices(std::vector<Slice> slices) {
    m_slices.clear();
    const int64_t frames = numFrames();
    for (auto& slice : slices) {
        slice.range.start = std::clamp<int64_t>(slice.range.start, 0, frames);
        slice.range.end = std::clamp<int64_t>(slice.range.end, 0, frames);
        if (slice.range.end <= slice.range.start) continue;
        m_slices.push_back(std::move(slice));
    }
}

void Document::clearSlices() { m_slices.clear(); }

const char* stretchAlgorithmName(StretchAlgorithm algorithm) {
    switch (algorithm) {
    case StretchAlgorithm::Wsola: return "wsola";
    case StretchAlgorithm::PhaseVocoder: return "phase_vocoder";
    case StretchAlgorithm::PaulStretch: return "paulstretch";
    }
    return "wsola";
}

bool stretchAlgorithmFromName(const std::string& name,
                              StretchAlgorithm& algorithm) {
    if (name == "wsola") algorithm = StretchAlgorithm::Wsola;
    else if (name == "phase_vocoder" || name == "phase-vocoder")
        algorithm = StretchAlgorithm::PhaseVocoder;
    else if (name == "paulstretch" || name == "paul_stretch")
        algorithm = StretchAlgorithm::PaulStretch;
    else return false;
    return true;
}

} // namespace vwave
