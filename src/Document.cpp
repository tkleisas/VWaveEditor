#include "vwave/Document.h"

#include "DocumentInternal.h"
#include "vwave/Processing.h"
#include "vwave/Spectral.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace vwave {

namespace {

} // namespace

using namespace detail;

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

Document::Document() = default;

Document::Document(Audio source) : m_source(std::move(source)) {}

Document Document::fromAudio(Audio source) {
    return Document(std::move(source));
}

const Audio& Document::audio() const {
    if (!m_cacheValid) {
        const_cast<Document*>(this)->rebuild();
    }
    if (!m_arrangedValid) {
        const_cast<Document*>(this)->refreshArranged();
    }
    return m_arranged;
}

void Document::refreshArranged() {
    m_arranged = m_clips.render(m_rendered);
    m_arrangedValid = true;
}

int Document::sampleRate() const { return audio().sampleRate(); }
int Document::numChannels() const { return audio().numChannels(); }
int64_t Document::numFrames() const { return audio().numFrames(); }
double Document::duration() const { return audio().duration(); }

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
    m_selection = {0, numFrames(), numFrames() > 0};
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

bool Document::hasClips() const {
    if (!m_cacheValid) const_cast<Document*>(this)->rebuild();
    return !m_clips.empty();
}

const std::vector<Clip>& Document::clips() const {
    if (!m_cacheValid) const_cast<Document*>(this)->rebuild();
    return m_clips.clips();
}

int Document::clipIndexAt(int64_t frame) const {
    if (!m_cacheValid) const_cast<Document*>(this)->rebuild();
    return m_clips.indexAt(frame);
}

bool Document::createClipArrangement(std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    if (!m_clips.empty()) return true;
    std::vector<Clip> clips;
    if (!m_clips.makeFull(m_rendered.numFrames(), clips, error)) return false;
    return setClipArrangement(std::move(clips), "Create clips", error);
}

bool Document::setClipArrangement(std::vector<Clip> clips,
                                  std::string label, std::string* error) {
    if (clips.empty()) {
        setError(error, "clip arrangement is empty");
        return false;
    }
    Operation operation = Operation::setClips(std::move(clips));
    if (!label.empty()) operation.label = std::move(label);
    return apply(operation, error);
}

bool Document::splitClipAt(int64_t frame, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.splitAt(frame, m_rendered.numFrames(), clips, error))
        return false;
    return setClipArrangement(std::move(clips), "Split clip", error);
}

bool Document::deleteClip(size_t index, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.remove(index, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Delete clip", error);
}

bool Document::moveClip(size_t index, int64_t timelineStart,
                        std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.move(index, timelineStart, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Move clip", error);
}

bool Document::stretchClip(size_t index, double ratio,
                           std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.stretch(index, ratio, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Stretch clip", error);
}

bool Document::repeatClip(size_t index, int count, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.repeat(index, count, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Repeat clip", error);
}

bool Document::setClipGain(size_t index, double gainDb,
                           std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.setGain(index, gainDb, clips, error)) return false;
    return setClipArrangement(std::move(clips), "Set clip gain", error);
}

bool Document::setClipFades(size_t index, double fadeInSeconds,
                            double fadeOutSeconds, std::string* error) {
    if (error) error->clear();
    if (!m_cacheValid) rebuild();
    std::vector<Clip> clips;
    if (!m_clips.setFades(index, fadeInSeconds, fadeOutSeconds, clips, error))
        return false;
    return setClipArrangement(std::move(clips), "Set clip fades", error);
}

bool Document::flattenClips(std::string* error) {
    return apply(Operation::flattenClips(), error);
}

void Document::clearClipboard() { m_clipboard = Audio{}; }

bool Document::addMarker(int64_t frame, std::string name) {
    if (frame < 0 || frame > numFrames()) return false;
    if (name.empty()) name = "Marker " + std::to_string(m_markers.size() + 1);
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
    m_slices = std::move(slices);
}

void Document::clearSlices() { m_slices.clear(); }

} // namespace vwave
