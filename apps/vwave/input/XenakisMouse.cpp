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
    const double viewStart = xenakisViewStart();
    const double viewDuration = xenakisViewDuration();
    double time = viewStart +
                  static_cast<double>(x - m_xenakisArea.x) /
                      static_cast<double>(m_xenakisArea.w) * viewDuration;
    time = std::clamp(time, 0.0, duration);
    if (snap) {
        const double quantum = viewDuration / 32.0;
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

int App::xenakisPointAt(int x, int y, int arcIndex) const {
    const xen::Page& page = m_xenakisController.page();
    if (arcIndex < 0 || arcIndex >= static_cast<int>(page.arcs.size()) ||
        m_xenakisArea.w <= 0 || m_xenakisArea.h <= 0)
        return -1;
    const xen::Arc& arc = page.arcs[static_cast<size_t>(arcIndex)];
    const double viewStart = xenakisViewStart();
    const double viewDuration = xenakisViewDuration();
    int best = -1;
    double bestDistance = 7.0;
    for (size_t i = 0; i < arc.points.size(); ++i) {
        const int px = m_xenakisArea.x + static_cast<int>(std::llround(
                                   (arc.points[i].time - viewStart) /
                                   viewDuration * m_xenakisArea.w));
        const int py = m_xenakisArea.y + static_cast<int>(std::llround(
                                   (1.0 - arc.points[i].pitch) *
                                   m_xenakisArea.h));
        const double dx = static_cast<double>(px - x);
        const double dy = static_cast<double>(py - y);
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void App::insertXenakisPoint(int arcIndex, double time, float pitch) {
    const xen::Page& page = m_xenakisController.page();
    if (arcIndex < 0 || arcIndex >= static_cast<int>(page.arcs.size()))
        return;
    const xen::Arc& arc = page.arcs[static_cast<size_t>(arcIndex)];
    if (!arc.valid() || time <= arc.start() + 1e-4 ||
        time >= arc.end() - 1e-4) {
        setStatus("Click inside the arc to add a point");
        return;
    }
    m_xenakisController.editPage(
        [arcIndex, time, pitch](xen::Page& edited) {
            if (arcIndex >= static_cast<int>(edited.arcs.size())) return;
            auto& points =
                edited.arcs[static_cast<size_t>(arcIndex)].points;
            const auto where = std::lower_bound(
                points.begin(), points.end(), time,
                [](const xen::ArcPoint& point, double value) {
                    return point.time < value;
                });
            points.insert(where, {static_cast<float>(time), pitch});
        },
        "arc_insert_" + std::to_string(arcIndex));
    const xen::Arc& updated =
        m_xenakisController.page().arcs[static_cast<size_t>(arcIndex)];
    m_xenakisSelectedArc = arcIndex;
    m_xenakisSelectedPoint = -1;
    double best = 1e-3;
    for (size_t i = 0; i < updated.points.size(); ++i) {
        const double distance =
            std::abs(static_cast<double>(updated.points[i].time) - time);
        if (distance < best) {
            best = distance;
            m_xenakisSelectedPoint = static_cast<int>(i);
        }
    }
    syncXenakisPageControls();
    setStatus("Point added");
}

void App::deleteXenakisPoint(int arcIndex, int pointIndex) {
    const xen::Page& page = m_xenakisController.page();
    if (arcIndex < 0 || arcIndex >= static_cast<int>(page.arcs.size()))
        return;
    const xen::Arc& arc = page.arcs[static_cast<size_t>(arcIndex)];
    if (pointIndex < 0 ||
        pointIndex >= static_cast<int>(arc.points.size()))
        return;
    if (arc.points.size() <= 2) {
        setStatus("An arc needs at least two points");
        return;
    }
    m_xenakisController.editPage(
        [arcIndex, pointIndex](xen::Page& edited) {
            if (arcIndex >= static_cast<int>(edited.arcs.size())) return;
            auto& points =
                edited.arcs[static_cast<size_t>(arcIndex)].points;
            if (pointIndex >= static_cast<int>(points.size())) return;
            if (points.size() <= 2) return;
            points.erase(points.begin() + pointIndex);
        },
        "arc_delete_point_" + std::to_string(arcIndex) + "_" +
            std::to_string(pointIndex));
    m_xenakisSelectedPoint = -1;
    syncXenakisPageControls();
    setStatus("Point deleted");
}

void App::handleXenakisMouseDown(const SDL_MouseButtonEvent& event) {
    SDL_Point point{event.x, event.y};
    if (!m_xenakisController.hasPage() ||
        !SDL_PointInRect(&point, &m_xenakisArea))
        return;
    if (m_xenakisController.previewing() &&
        event.button == SDL_BUTTON_LEFT) {
        if (m_xenakisController.seekPreview(
                xenakisTimeAt(event.x, false)))
            m_xenakisScrubbingPreview = true;
        return;
    }
    const int hit = xenakisArcAt(event.x, event.y);

    if (event.button == SDL_BUTTON_RIGHT) {
        if (hit >= 0) {
            const int pointIndex =
                xenakisPointAt(event.x, event.y, hit);
            if (pointIndex >= 0)
                deleteXenakisPoint(hit, pointIndex);
            else
                setStatus("Right-click a breakpoint to delete it");
        }
        return;
    }

    if (m_xenakisTool == 2) {
        if (hit >= 0) {
            m_xenakisController.editPage([hit](xen::Page& page) {
                if (hit < static_cast<int>(page.arcs.size()))
                    page.arcs.erase(page.arcs.begin() + hit);
            });
            m_xenakisSelectedArc = -1;
            m_xenakisSelectedPoint = -1;
            syncXenakisPageControls();
            setStatus("Arc erased");
        }
        return;
    }

    if (m_xenakisTool == 1 && hit >= 0 &&
        hit == m_xenakisSelectedArc) {
        const int point =
            xenakisPointAt(event.x, event.y, m_xenakisSelectedArc);
        if (point >= 0) {
            m_xenakisSelectedPoint = point;
            m_xenakisDraggingPoint = true;
            return;
        }
    }

    if (m_xenakisTool != 2 && hit >= 0 && event.clicks >= 2) {
        const double time = xenakisTimeAt(event.x, false);
        insertXenakisPoint(
            hit, time,
            static_cast<float>(m_xenakisController.page()
                                   .arcs[static_cast<size_t>(hit)]
                                   .pitchAt(time)));
        return;
    }

    if (m_xenakisTool == 1 || hit >= 0) {
        m_xenakisSelectedArc = hit;
        m_xenakisSelectedPoint = -1;
        syncXenakisPageControls();
        setStatus(hit >= 0 ? "Arc selected" : "Selection cleared");
        return;
    }

    m_xenakisSelectedPoint = -1;
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
    if (m_xenakisScrubbingPreview) {
        if (m_xenakisController.previewing()) {
            m_xenakisController.seekPreview(
                xenakisTimeAt(event.x, false));
            return;
        }
        m_xenakisScrubbingPreview = false;
    }
    if (m_xenakisDraggingPoint && m_xenakisSelectedArc >= 0 &&
        m_xenakisSelectedPoint >= 0) {
        const xen::Page& page = m_xenakisController.page();
        const int arcIndex = m_xenakisSelectedArc;
        const int pointIndex = m_xenakisSelectedPoint;
        if (arcIndex < static_cast<int>(page.arcs.size()) &&
            pointIndex < static_cast<int>(
                             page.arcs[static_cast<size_t>(arcIndex)]
                                 .points.size())) {
            const xen::Arc& arc =
                page.arcs[static_cast<size_t>(arcIndex)];
            double time = xenakisTimeAt(event.x, m_xenakisSnapTime);
            if (pointIndex > 0)
                time = std::max(
                    time,
                    static_cast<double>(
                        arc.points[static_cast<size_t>(pointIndex - 1)]
                            .time) +
                        1e-4);
            if (pointIndex + 1 <
                static_cast<int>(arc.points.size()))
                time = std::min(
                    time,
                    static_cast<double>(
                        arc.points[static_cast<size_t>(pointIndex + 1)]
                            .time) -
                        1e-4);
            const float pitch =
                xenakisPitchAt(event.y, m_xenakisSnapPitch);
            m_xenakisController.editPage(
                [arcIndex, pointIndex, time, pitch](xen::Page& edited) {
                    if (arcIndex <
                            static_cast<int>(edited.arcs.size()) &&
                        pointIndex < static_cast<int>(
                                         edited.arcs[static_cast<size_t>(
                                                         arcIndex)]
                                             .points.size()))
                        edited.arcs[static_cast<size_t>(arcIndex)]
                            .points[static_cast<size_t>(pointIndex)] = {
                            static_cast<float>(time), pitch};
                },
                "arc_point_" + std::to_string(arcIndex) + "_" +
                    std::to_string(pointIndex));
        }
        return;
    }
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
    if (m_xenakisScrubbingPreview) {
        m_xenakisScrubbingPreview = false;
        return;
    }
    if (m_xenakisDraggingPoint) {
        m_xenakisDraggingPoint = false;
        setStatus("Point moved");
        return;
    }
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
