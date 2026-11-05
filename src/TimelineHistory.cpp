#include "vwave/Timeline.h"

#include <algorithm>
#include <utility>

namespace vwave {

Timeline::TimelineState Timeline::captureState() const {
    TimelineState state;
    state.tracks = m_tracks;
    state.sampleRate = m_sampleRate;
    state.active = m_active;
    state.selection = m_selection;
    state.selectionScope = m_selectionScope;
    return state;
}

void Timeline::restoreState(TimelineState state) {
    m_tracks = std::move(state.tracks);
    m_sampleRate = state.sampleRate;
    m_active = state.active;
    m_selection = state.selection;
    m_selectionScope = state.selectionScope;
}

bool Timeline::recordUndo(const std::string& label,
                          const std::string& coalesceKey) {
    if (!coalesceKey.empty() && !m_undo.empty() &&
        m_undo.back().coalesceKey == coalesceKey &&
        m_undo.back().stamp.sequence == currentHistorySequence() &&
        !historyRedoPending()) {
        return false;
    }
    const HistoryStamp stamp = nextHistoryStampForNewAction();
    m_undo.push_back({stamp, captureState(), label, coalesceKey});
    m_redo.clear();
    if (m_undo.size() > kMaxUndoEntries) m_undo.erase(m_undo.begin());
    return true;
}

void Timeline::recordUndoAfter(TimelineState before, const std::string& label,
                               const std::string& coalesceKey) {
    const HistoryStamp stamp = nextHistoryStampForNewAction();
    m_undo.push_back({stamp, std::move(before), label, coalesceKey});
    m_redo.clear();
    if (m_undo.size() > kMaxUndoEntries) m_undo.erase(m_undo.begin());
}

void Timeline::discardStaleTimelineRedo() const {
    const uint64_t generation = currentHistoryGeneration();
    m_redo.erase(
        std::remove_if(m_redo.begin(), m_redo.end(),
                       [generation](const UndoEntry& entry) {
                           return entry.stamp.generation != generation;
                       }),
        m_redo.end());
}

bool Timeline::latestDocumentUndo(int& index, HistoryStamp& stamp) const {
    bool found = false;
    for (int i = 0; i < trackCount(); ++i) {
        const auto& document = m_tracks[static_cast<size_t>(i)].document;
        if (!document.canUndo()) continue;
        const HistoryStamp candidate = document.latestUndoStamp();
        if (!found || candidate.sequence > stamp.sequence) {
            found = true;
            index = i;
            stamp = candidate;
        }
    }
    return found;
}

bool Timeline::latestDocumentRedo(int& index, HistoryStamp& stamp) const {
    bool found = false;
    for (int i = 0; i < trackCount(); ++i) {
        const auto& document = m_tracks[static_cast<size_t>(i)].document;
        if (!document.canRedo()) continue;
        const HistoryStamp candidate = document.latestRedoStamp();
        if (!found || candidate.sequence < stamp.sequence) {
            found = true;
            index = i;
            stamp = candidate;
        }
    }
    return found;
}

void Timeline::syncSelectionFromActiveDocument() {
    if (m_active < 0 || m_active >= trackCount()) {
        m_selection.clear();
        return;
    }
    const auto& track = m_tracks[static_cast<size_t>(m_active)];
    const auto& selection = track.document.selection();
    if (selection.active && selection.end > selection.start) {
        m_selection = {selection.start + track.startFrame,
                       selection.end + track.startFrame, true};
    } else {
        m_selection.clear();
    }
    syncSelectionToTracks();
}

bool Timeline::canUndo() const {
    if (!m_undo.empty()) return true;
    int index = -1;
    HistoryStamp stamp;
    return latestDocumentUndo(index, stamp);
}

bool Timeline::canRedo() const {
    discardStaleTimelineRedo();
    if (!m_redo.empty()) return true;
    int index = -1;
    HistoryStamp stamp;
    return latestDocumentRedo(index, stamp);
}

size_t Timeline::undoCount() const {
    size_t count = m_undo.size();
    for (const auto& track : m_tracks)
        count += track.document.undoCount();
    return count;
}

size_t Timeline::redoCount() const {
    discardStaleTimelineRedo();
    size_t count = m_redo.size();
    for (const auto& track : m_tracks)
        count += track.document.redoCount();
    return count;
}

bool Timeline::undo() {
    int documentIndex = -1;
    HistoryStamp documentStamp;
    const bool documentAvailable =
        latestDocumentUndo(documentIndex, documentStamp);
    if (!documentAvailable && m_undo.empty()) return false;

    if (documentAvailable &&
        (m_undo.empty() ||
         documentStamp.sequence > m_undo.back().stamp.sequence)) {
        auto& document =
            m_tracks[static_cast<size_t>(documentIndex)].document;
        if (!document.undo()) return false;
        m_active = documentIndex;
        syncSelectionFromActiveDocument();
        return true;
    }
    return undoTimeline();
}

bool Timeline::redoTimeline() {
    UndoEntry entry = std::move(m_redo.back());
    m_redo.pop_back();
    TimelineState current = captureState();
    const HistoryStamp stamp = entry.stamp;
    const std::string label = entry.label;
    restoreState(std::move(entry.state));
    m_undo.push_back({stamp, std::move(current), label, {}});
    if (m_undo.size() > kMaxUndoEntries) m_undo.erase(m_undo.begin());
    return true;
}

bool Timeline::undoTimeline() {
    UndoEntry entry = std::move(m_undo.back());
    m_undo.pop_back();
    TimelineState current = captureState();
    const HistoryStamp stamp = entry.stamp;
    const std::string label = entry.label;
    restoreState(std::move(entry.state));
    m_redo.push_back({stamp, std::move(current), label, {}});
    noteUndoPerformed();
    return true;
}

bool Timeline::redo() {
    discardStaleTimelineRedo();
    int documentIndex = -1;
    HistoryStamp documentStamp;
    const bool documentAvailable =
        latestDocumentRedo(documentIndex, documentStamp);
    if (!documentAvailable && m_redo.empty()) return false;

    if (documentAvailable &&
        (m_redo.empty() ||
         documentStamp.sequence < m_redo.back().stamp.sequence)) {
        auto& document =
            m_tracks[static_cast<size_t>(documentIndex)].document;
        if (!document.redo()) return false;
        m_active = documentIndex;
        syncSelectionFromActiveDocument();
        return true;
    }
    return redoTimeline();
}

std::string Timeline::undoLabel() const {
    int documentIndex = -1;
    HistoryStamp documentStamp;
    const bool documentAvailable =
        latestDocumentUndo(documentIndex, documentStamp);
    if (!documentAvailable)
        return m_undo.empty() ? std::string{} : m_undo.back().label;
    if (m_undo.empty() || documentStamp.sequence > m_undo.back().stamp.sequence)
        return m_tracks[static_cast<size_t>(documentIndex)]
            .document.undoLabel();
    return m_undo.back().label;
}

std::string Timeline::redoLabel() const {
    discardStaleTimelineRedo();
    int documentIndex = -1;
    HistoryStamp documentStamp;
    const bool documentAvailable =
        latestDocumentRedo(documentIndex, documentStamp);
    if (!documentAvailable)
        return m_redo.empty() ? std::string{} : m_redo.back().label;
    if (m_redo.empty() || documentStamp.sequence < m_redo.back().stamp.sequence)
        return m_tracks[static_cast<size_t>(documentIndex)]
            .document.redoLabel();
    return m_redo.back().label;
}

void Timeline::clearUndoHistory() {
    m_undo.clear();
    m_redo.clear();
}

} // namespace vwave
