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
    if (event.button != SDL_BUTTON_LEFT ||
        m_scopeController.shape() != 0)
        return;
    SDL_Point point{event.x, event.y};
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
