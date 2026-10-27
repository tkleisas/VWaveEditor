#pragma once

#include <SDL.h>

#include <algorithm>
#include <cstdint>

namespace vwave::app {

class LoopController {
public:
    bool enabled() const { return m_enabled; }
    int64_t start() const { return m_start; }
    int64_t end() const { return m_end; }
    int64_t anchor() const { return m_anchor; }
    const SDL_Rect& dragArea() const { return m_dragArea; }

    bool active(int64_t totalFrames) const {
        if (!m_enabled) return false;
        const int64_t total = std::max<int64_t>(0, totalFrames);
        return std::clamp<int64_t>(m_end, 0, total) >
               std::clamp<int64_t>(m_start, 0, total);
    }

    void setEnabled(bool enabled) { m_enabled = enabled; }

    void clear() {
        m_enabled = false;
        m_start = 0;
        m_end = 0;
        m_anchor = 0;
        m_dragArea = {};
    }

    void clamp(int64_t totalFrames) {
        const int64_t total = std::max<int64_t>(0, totalFrames);
        m_start = std::clamp<int64_t>(m_start, 0, total);
        m_end = std::clamp<int64_t>(m_end, 0, total);
        if (m_end <= m_start) clear();
    }

    void setRegion(int64_t start, int64_t end, int64_t totalFrames) {
        const int64_t total = std::max<int64_t>(0, totalFrames);
        start = std::clamp<int64_t>(start, 0, total);
        end = std::clamp<int64_t>(end, 0, total);
        if (end <= start) {
            clear();
            return;
        }
        m_start = start;
        m_end = end;
        m_enabled = true;
    }

    void beginDrag(int64_t frame, const SDL_Rect& area) {
        m_dragArea = area;
        m_anchor = std::max<int64_t>(0, frame);
        m_start = m_anchor;
        m_end = m_anchor;
        m_enabled = true;
    }

    void updateDrag(int64_t frame) {
        frame = std::max<int64_t>(0, frame);
        m_start = std::min(m_anchor, frame);
        m_end = std::max(m_anchor, frame);
        m_enabled = true;
    }

    void finishDrag(int64_t totalFrames) {
        clamp(totalFrames);
        if (m_end > m_start) m_enabled = true;
    }

private:
    bool m_enabled = false;
    int64_t m_start = 0;
    int64_t m_end = 0;
    int64_t m_anchor = 0;
    SDL_Rect m_dragArea{};
};

} // namespace vwave::app
