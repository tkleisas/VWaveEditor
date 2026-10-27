#include "vwave/Document.h"

#include "DocumentInternal.h"

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
