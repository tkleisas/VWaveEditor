#include "App.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kXenakisHitTolerancePx = 8.0;

} // namespace

double App::xenakisTimeAt(int x, bool snap) const {
    const xen::Page& page = m_xenakisController.page();
    const double duration = std::max(0.001, page.duration);
    if (m_xenakisArea.w <= 0) return 0.0;
    double time = static_cast<double>(x - m_xenakisArea.x) /
                  static_cast<double>(m_xenakisArea.w) * duration;
    time = std::clamp(time, 0.0, duration);
    if (snap) {
        const double quantum = duration / 32.0;
        if (quantum > 0.0)
            time = std::round(time / quantum) * quantum;
    }
    return std::clamp(time, 0.0, duration);
}

float App::xenakisPitchAt(int y, bool snap) const {
    const xen::Page& page = m_xenakisController.page();
    if (m_xenakisArea.h <= 0) return 0.5f;
    double pitch = 1.0 - static_cast<double>(y - m_xenakisArea.y) /
                              static_cast<double>(m_xenakisArea.h);
    pitch = std::clamp(pitch, 0.0, 1.0);
    if (snap && !page.frequencyTables.empty()) {
        const xen::FrequencyTable& table = page.frequencyTables.front();
        double index = table.indexForNorm(pitch);
        if (table.useSieve && !table.sieve.empty()) {
            const int64_t base = static_cast<int64_t>(std::llround(index));
            for (int64_t distance = 0; distance <= 96; ++distance) {
                if (table.sieve.matches(base + distance)) {
                    index = static_cast<double>(base + distance);
                    break;
                }
                if (distance != 0 &&
                    table.sieve.matches(base - distance)) {
                    index = static_cast<double>(base - distance);
                    break;
                }
            }
        } else {
            index = std::round(index);
        }
        const double span =
            std::max(1e-9, table.highIndex - table.lowIndex);
        pitch = (index - table.lowIndex) / span;
    }
    return static_cast<float>(std::clamp(pitch, 0.0, 1.0));
}

int App::xenakisArcAt(int x, int y) const {
    const xen::Page& page = m_xenakisController.page();
    if (page.arcs.empty() || m_xenakisArea.w <= 0) return -1;
    const double time = xenakisTimeAt(x, false);
    int best = -1;
    double bestDistance = kXenakisHitTolerancePx;
    for (size_t i = 0; i < page.arcs.size(); ++i) {
        const xen::Arc& arc = page.arcs[i];
        if (!arc.valid() || time < arc.start() - 1e-9 ||
            time > arc.end() + 1e-9)
            continue;
        const double pitch =
            std::clamp<double>(arc.pitchAt(time), 0.0, 1.0);
        const int arcY = m_xenakisArea.y + static_cast<int>(
                                              std::llround(
                                                  (1.0 - pitch) *
                                                  m_xenakisArea.h));
        const double distance = std::abs(arcY - y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void App::handleXenakisMouseDown(const SDL_MouseButtonEvent& event) {
    SDL_Point point{event.x, event.y};
    if (!m_xenakisController.hasPage() ||
        !SDL_PointInRect(&point, &m_xenakisArea))
        return;
    const int hit = xenakisArcAt(event.x, event.y);

    if (m_xenakisTool == 2) {
        if (hit >= 0) {
            m_xenakisController.editPage([hit](xen::Page& page) {
                if (hit < static_cast<int>(page.arcs.size()))
                    page.arcs.erase(page.arcs.begin() + hit);
            });
            m_xenakisSelectedArc = -1;
            syncXenakisPageControls();
            setStatus("Arc erased");
        }
        return;
    }

    if (m_xenakisTool == 1 || hit >= 0) {
        m_xenakisSelectedArc = hit;
        syncXenakisPageControls();
        setStatus(hit >= 0 ? "Arc selected" : "Selection cleared");
        return;
    }

    m_xenakisDrawing = true;
    m_xenakisDrawStartTime =
        static_cast<float>(xenakisTimeAt(event.x, m_xenakisSnapTime));
    m_xenakisDrawCurrentTime = m_xenakisDrawStartTime;
    m_xenakisDrawStartPitch =
        xenakisPitchAt(event.y, m_xenakisSnapPitch);
    m_xenakisDrawCurrentPitch = m_xenakisDrawStartPitch;
    m_xenakisFreehandPoints.clear();
    m_xenakisFreehandPoints.push_back(
        {m_xenakisDrawStartTime, m_xenakisDrawStartPitch});
}

void App::handleXenakisMouseMotion(const SDL_MouseMotionEvent& event) {
    if (!m_xenakisDrawing) return;
    m_xenakisDrawCurrentTime =
        static_cast<float>(xenakisTimeAt(event.x, m_xenakisSnapTime));
    m_xenakisDrawCurrentPitch =
        xenakisPitchAt(event.y, m_xenakisSnapPitch);
    if (m_xenakisFreehand && m_xenakisArea.w > 0 &&
        m_xenakisArea.h > 0) {
        const double duration =
            std::max(0.001, m_xenakisController.page().duration);
        const xen::ArcPoint& last = m_xenakisFreehandPoints.back();
        const double dx =
            (static_cast<double>(m_xenakisDrawCurrentTime) - last.time) /
            duration * m_xenakisArea.w;
        const double dy =
            (static_cast<double>(m_xenakisDrawCurrentPitch) - last.pitch) *
            m_xenakisArea.h;
        if (dx * dx + dy * dy >= 4.0)
            m_xenakisFreehandPoints.push_back(
                {m_xenakisDrawCurrentTime, m_xenakisDrawCurrentPitch});
    }
}

void App::handleXenakisMouseUp(const SDL_MouseButtonEvent& event) {
    (void)event;
    if (!m_xenakisDrawing) return;
    m_xenakisDrawing = false;

    const float startTime = m_xenakisDrawStartTime;
    const float endTime = m_xenakisDrawCurrentTime;
    const float from = std::min(startTime, endTime);
    const float to = std::max(startTime, endTime);

    std::vector<xen::ArcPoint> points;
    if (m_xenakisFreehand && m_xenakisFreehandPoints.size() >= 2) {
        points = m_xenakisFreehandPoints;
        points.push_back(
            {m_xenakisDrawCurrentTime, m_xenakisDrawCurrentPitch});
        std::sort(points.begin(), points.end(),
                  [](const xen::ArcPoint& a, const xen::ArcPoint& b) {
                      return a.time < b.time;
                  });
        points.erase(std::unique(points.begin(), points.end(),
                                 [](const xen::ArcPoint& a,
                                    const xen::ArcPoint& b) {
                                     return std::abs(a.time - b.time) <
                                            1e-6f;
                                 }),
                     points.end());
    } else {
        if (to - from < 1e-3f) {
            m_xenakisFreehandPoints.clear();
            return;
        }
        points = {{from, startTime <= endTime ? m_xenakisDrawStartPitch
                                              : m_xenakisDrawCurrentPitch},
                  {to, startTime <= endTime ? m_xenakisDrawCurrentPitch
                                            : m_xenakisDrawStartPitch}};
    }
    m_xenakisFreehandPoints.clear();

    if (points.size() < 2 ||
        points.back().time - points.front().time < 1e-3f)
        return;

    xen::Arc arc;
    arc.waveform = static_cast<xen::WaveformType>(
        std::clamp(m_xenakisWaveform, 0, 4));
    arc.weight = static_cast<float>(m_xenakisArcWeight.value);
    arc.group = std::clamp(m_xenakisGroup, 0, 3);
    arc.points = std::move(points);

    m_xenakisController.editPage(
        [&arc](xen::Page& page) { page.arcs.push_back(arc); });
    m_xenakisSelectedArc =
        static_cast<int>(m_xenakisController.page().arcs.size()) - 1;
    syncXenakisPageControls();
    setStatus("Arc added");
}
