#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace vwave::app;
using namespace vwave::ui;

void App::handleOscilloscopeMouseDown(const SDL_MouseButtonEvent& event) {
    if (event.button != SDL_BUTTON_LEFT) return;
    SDL_Point point{event.x, event.y};
    if (m_scopeTimelineArea.w > 0 &&
        SDL_PointInRect(&point, &m_scopeTimelineArea)) {
        const double duration = std::max(0.01, m_scopeDuration.value);
        const int left = m_scopeTimelineArea.x + 10;
        const int right =
            m_scopeTimelineArea.x + m_scopeTimelineArea.w - 10;
        const double fraction = std::clamp(
            static_cast<double>(event.x - left) /
                static_cast<double>(std::max(1, right - left)),
            0.0, 1.0);
        const double time = fraction * duration;
        const auto& keyframes = m_scopeController.keyframes();
        int nearest = -1;
        double nearestDistance = 8.0;
        for (size_t i = 0; i < keyframes.size(); ++i) {
            const double keyX =
                left + std::clamp(keyframes[i].time / duration, 0.0, 1.0) *
                           static_cast<double>(right - left);
            const double distance = std::abs(keyX - event.x);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = static_cast<int>(i);
            }
        }
        if (nearest < 0 && event.clicks >= 2) {
            m_scopeKeyTime.setValue(time);
            m_scopeController.addKeyframe(time);
            syncScopeKeyframeControls();
            return;
        }
        m_scopeController.setSelectedKeyframe(nearest);
        m_scopeKeyTime.setValue(time);
        if (nearest >= 0) syncScopeKeyframeControls();
        return;
    }
    if (!m_scopeController.pointEditing()) return;
    if (!SDL_PointInRect(&point, &m_scopeArea) || m_scopeArea.w <= 0 ||
        m_scopeArea.h <= 0)
        return;
    const double x =
        (static_cast<double>(event.x - m_scopeArea.x) /
         static_cast<double>(m_scopeArea.w)) *
            2.0 -
        1.0;
    const double y =
        1.0 -
        (static_cast<double>(event.y - m_scopeArea.y) /
         static_cast<double>(m_scopeArea.h)) *
            2.0;
    const vwave::ScopePoint candidate{std::clamp(x, -1.0, 1.0),
                                      std::clamp(y, -1.0, 1.0)};
    auto& path = m_scopeController.path();
    if (path.points.size() >= 3) {
        const auto& first = path.points.front();
        if (std::hypot(candidate.x - first.x, candidate.y - first.y) <
            0.06) {
            path.closed = true;
            setStatus("Scope path closed");
            return;
        }
    }
    path.name = "Path";
    path.points.push_back(candidate);
    path.closed = false;
    setStatus("Scope point " +
              std::to_string(path.points.size()) +
              " added");

}
