#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <algorithm>
#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

std::vector<Operation> Document::operationHistory() const {
    std::vector<Operation> result;
    result.reserve(m_history.size());
    for (const auto& entry : m_history) result.push_back(entry.operation);
    return result;
}

void Document::captureCheckpoint() {
    captureCheckpoint(m_history.size(), m_rendered, m_clips.clips(),
                      m_selection);
}

void Document::captureCheckpoint(size_t historySize, const Audio& rendered,
                                 const std::vector<Clip>& clips,
                                 const Selection& selection) {
    if (historySize == 0 || historySize % kCheckpointInterval != 0) return;
    m_checkpoints.push_back(
        {historySize, rendered, clips, selection});
    if (m_checkpoints.size() > kMaxCheckpoints) m_checkpoints.erase(
        m_checkpoints.begin());
}

void Document::dropCheckpointsAtOrAfter(size_t historySize) {
    m_checkpoints.erase(
        std::remove_if(m_checkpoints.begin(), m_checkpoints.end(),
                       [historySize](const Checkpoint& checkpoint) {
                           return checkpoint.historySize >= historySize;
                       }),
        m_checkpoints.end());
}

const Document::Checkpoint* Document::checkpointFor(
    size_t historySize) const {
    for (auto it = m_checkpoints.rbegin(); it != m_checkpoints.rend(); ++it) {
        if (it->historySize <= historySize) return &*it;
    }
    return nullptr;
}

bool Document::undo() {
    if (!m_cacheValid) rebuild();
    if (m_history.empty()) return false;
    HistoryEntry entry = std::move(m_history.back());
    m_history.pop_back();
    m_redo.push_back(entry);
    if (!rebuild()) return false;
    ++m_revision;
    m_selection = clampSelection(entry.before, m_rendered.numFrames());
    noteUndoPerformed();
    return true;
}

bool Document::redo() {
    if (!m_cacheValid) rebuild();
    discardStaleRedo();
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
    ++m_revision;
    m_selection = clampSelection(entry.after, m_arranged.numFrames());
    return true;
}

bool Document::canRedo() const { return redoCount() > 0; }

size_t Document::redoCount() const {
    const uint64_t generation = currentHistoryGeneration();
    size_t count = 0;
    for (const auto& entry : m_redo)
        if (entry.stamp.generation == generation) ++count;
    return count;
}

HistoryStamp Document::latestUndoStamp() const {
    return m_history.empty() ? HistoryStamp{} : m_history.back().stamp;
}

HistoryStamp Document::latestRedoStamp() const {
    const uint64_t generation = currentHistoryGeneration();
    for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it)
        if (it->stamp.generation == generation) return it->stamp;
    return {};
}

void Document::discardStaleRedo() {
    const uint64_t generation = currentHistoryGeneration();
    m_redo.erase(
        std::remove_if(m_redo.begin(), m_redo.end(),
                       [generation](const HistoryEntry& entry) {
                           return entry.stamp.generation != generation;
                       }),
        m_redo.end());
}

std::string Document::undoLabel() const {
    return m_history.empty() ? std::string{} : m_history.back().operation.label;
}

std::string Document::redoLabel() const {
    const uint64_t generation = currentHistoryGeneration();
    for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it)
        if (it->stamp.generation == generation)
            return it->operation.label;
    return {};
}

void Document::clearHistory() {
    if (!m_cacheValid) rebuild();
    if (!m_clips.empty()) m_rendered = m_clips.render(m_rendered);
    m_source = m_rendered;
    m_clips.clear();
    m_arranged = m_rendered;
    m_history.clear();
    m_redo.clear();
    m_checkpoints.clear();
    m_cacheValid = true;
    m_arrangedValid = true;
    ++m_revision;
    m_selection = clampSelection(m_selection, m_source.numFrames());
}

bool Document::rebuild() {
    Audio working = m_source;
    std::vector<Clip> clips;
    Selection selection;
    size_t firstOperation = 0;
    if (const Checkpoint* checkpoint = checkpointFor(m_history.size())) {
        working = checkpoint->rendered;
        clips = checkpoint->clips;
        selection = checkpoint->selection;
        firstOperation = checkpoint->historySize;
    }
    for (size_t i = firstOperation; i < m_history.size(); ++i) {
        const auto& entry = m_history[i];
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
