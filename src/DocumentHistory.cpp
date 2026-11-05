#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <algorithm>
#include <utility>

using namespace vwave::detail;

namespace vwave {

std::vector<Operation> Document::operationHistory() const {
    std::vector<Operation> result;
    result.reserve(m_history.size());
    for (const auto& entry : m_history) result.push_back(entry.operation);
    return result;
}

bool Document::undo() {
    if (m_history.empty()) return false;
    HistoryEntry entry = std::move(m_history.back());
    m_history.pop_back();
    m_arrangement.assign(entry.before);
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
    m_selection = clampSelection(entry.beforeSelection, numFrames());
    m_redo.push_back(std::move(entry));
    noteUndoPerformed();
    return true;
}

bool Document::redo() {
    discardStaleRedo();
    if (m_redo.empty()) return false;
    HistoryEntry entry = std::move(m_redo.back());
    m_redo.pop_back();
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
    m_selection = clampSelection(entry.afterSelection, numFrames());
    m_history.push_back(std::move(entry));
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

} // namespace vwave
