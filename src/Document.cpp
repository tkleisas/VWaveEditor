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

std::vector<Operation> Document::operationHistory() const {
    std::vector<Operation> result;
    result.reserve(m_history.size());
    for (const auto& entry : m_history) result.push_back(entry.operation);
    return result;
}

bool Document::undo() {
    if (!m_cacheValid) rebuild();
    if (m_history.empty()) return false;
    HistoryEntry entry = std::move(m_history.back());
    m_history.pop_back();
    m_redo.push_back(entry);
    if (!rebuild()) return false;
    m_selection = clampSelection(entry.before, m_rendered.numFrames());
    return true;
}

bool Document::redo() {
    if (!m_cacheValid) rebuild();
    if (m_redo.empty()) return false;
    HistoryEntry entry = std::move(m_redo.back());
    m_redo.pop_back();

    Audio base = m_rendered;
    std::vector<Clip> clips = m_clips.clips();
    Selection after = entry.after;
    if (!applyHistoryOperation(entry.operation, base, clips, after, nullptr)) {
        m_redo.push_back(std::move(entry));
        return false;
    }
    m_rendered = std::move(base);
    m_clips.assign(std::move(clips));
    m_arranged = m_clips.render(m_rendered);
    m_arrangedValid = true;
    m_cacheValid = true;
    m_history.push_back(entry);
    m_selection = clampSelection(entry.after, m_arranged.numFrames());
    return true;
}

std::string Document::undoLabel() const {
    return m_history.empty() ? std::string{} : m_history.back().operation.label;
}

std::string Document::redoLabel() const {
    return m_redo.empty() ? std::string{} : m_redo.back().operation.label;
}

void Document::clearHistory() {
    if (!m_cacheValid) rebuild();
    if (!m_clips.empty()) m_rendered = m_clips.render(m_rendered);
    m_source = m_rendered;
    m_clips.clear();
    m_arranged = m_rendered;
    m_history.clear();
    m_redo.clear();
    m_cacheValid = true;
    m_arrangedValid = true;
    m_selection = clampSelection(m_selection, m_source.numFrames());
}

bool Document::gainDb(double db, std::string* error) {
    return apply(Operation::gain(db, activeRange()), error);
}

bool Document::normalize(double peakDb, std::string* error) {
    return apply(Operation::normalize(peakDb, activeRange()), error);
}

bool Document::fadeIn(std::string* error) {
    return apply(Operation::fadeIn(activeRange()), error);
}

bool Document::fadeOut(std::string* error) {
    return apply(Operation::fadeOut(activeRange()), error);
}

bool Document::reverseSelection(std::string* error) {
    return apply(Operation::reverse(activeRange()), error);
}

bool Document::invertSelection(std::string* error) {
    return apply(Operation::invert(activeRange()), error);
}

bool Document::silenceSelection(std::string* error) {
    return apply(Operation::silence(activeRange()), error);
}

bool Document::removeDc(std::string* error) {
    return apply(Operation::dcRemove(activeRange()), error);
}

bool Document::timeStretch(double ratio, StretchAlgorithm algorithm,
                           std::string* error) {
    return apply(Operation::timeStretch(ratio, activeRange(), algorithm), error);
}

bool Document::timeStretchTrack(double ratio, StretchAlgorithm algorithm,
                                std::string* error) {
    if (!m_cacheValid) rebuild();
    if (m_rendered.empty()) {
        setError(error, "document is empty");
        return false;
    }
    return apply(Operation::timeStretch(ratio, {0, m_rendered.numFrames()},
                                        algorithm),
                 error);
}

bool Document::pitchShift(double semitones, StretchAlgorithm algorithm,
                          std::string* error) {
    return apply(Operation::pitchShift(semitones, activeRange(), algorithm),
                 error);
}

bool Document::spectralGain(double lowHz, double highHz, double gainDb,
                            std::string* error) {
    return apply(
        Operation::spectralGain(lowHz, highHz, gainDb, activeRange()), error);
}

bool Document::spectralFilter(const std::string& type, double cutoffHz,
                              std::string* error) {
    return apply(Operation::spectralFilter(type, cutoffHz, activeRange()),
                 error);
}

bool Document::spectralGate(double thresholdDb, std::string* error) {
    return apply(Operation::spectralGate(thresholdDb, activeRange()), error);
}

bool Document::spectralBlur(int timeRadius, int frequencyRadius,
                            std::string* error) {
    return apply(
        Operation::spectralBlur(timeRadius, frequencyRadius, activeRange()),
        error);
}

bool Document::convolve(const Audio& impulseResponse, bool normalizeIr,
                        std::string* error) {
    return apply(Operation::convolve(impulseResponse, activeRange(), normalizeIr),
                 error);
}

bool Document::copySelection() {
    if (!m_cacheValid) rebuild();
    if (!m_selection.active || m_selection.empty()) return false;
    m_clipboard = audio().slice(m_selection.range());
    return true;
}

bool Document::cutSelection(std::string* error) {
    if (!m_cacheValid) rebuild();
    if (!m_selection.active || m_selection.empty()) return false;
    if (!copySelection()) return false;
    return deleteSelection(error);
}

bool Document::deleteSelection(std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::remove(m_selection.range()), error);
}

bool Document::moveSelectionTo(int64_t destination, bool copy,
                               std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::moveRange(m_selection.range(), destination, copy),
                 error);
}

bool Document::repeatSelection(int repeats, std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::repeatRange(m_selection.range(), repeats), error);
}

bool Document::paste(std::string* error) {
    if (m_clipboard.empty()) {
        setError(error, "clipboard is empty");
        return false;
    }
    if (m_selection.active && !m_selection.empty()) {
        return apply(Operation::replaceRange(m_selection.range(), m_clipboard),
                     error);
    }
    return apply(
        Operation::insert(m_rendered.numFrames(), m_clipboard), error);
}

bool Document::pasteAt(int64_t frame, std::string* error) {
    if (m_clipboard.empty()) {
        setError(error, "clipboard is empty");
        return false;
    }
    return apply(Operation::insert(frame, m_clipboard), error);
}

bool Document::cropToSelection(std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::crop(m_selection.range()), error);
}

bool Document::insertAudio(int64_t frame, Audio audio, std::string* error) {
    return apply(Operation::insert(frame, std::move(audio)), error);
}

bool Document::replaceSelection(Audio audio, std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::replaceRange(m_selection.range(), std::move(audio)),
                 error);
}

bool Document::resample(int sampleRate, std::string* error) {
    return apply(Operation::resample(sampleRate), error);
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

bool Document::rebuild() {
    Audio working = m_source;
    std::vector<Clip> clips;
    Selection selection;
    for (const auto& entry : m_history) {
        Selection after = selection;
        if (!applyHistoryOperation(entry.operation, working, clips, after,
                                   nullptr)) {
            m_rendered = std::move(working);
            m_clips.assign(std::move(clips));
            m_arranged = m_clips.render(m_rendered);
            m_arrangedValid = true;
            m_cacheValid = true;
            return false;
        }
        selection = after;
    }
    m_rendered = std::move(working);
    m_clips.assign(std::move(clips));
    m_arranged = m_clips.render(m_rendered);
    m_arrangedValid = true;
    m_cacheValid = true;
    m_selection = clampSelection(m_selection, m_arranged.numFrames());
    return true;
}

} // namespace vwave
