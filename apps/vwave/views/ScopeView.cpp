#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace vwave::ui;

void App::drawOscilloscope(const SDL_Rect& area) {
    if (!m_scopePath.valid()) {
        drawText("No scope path", area.x + 40, area.y + 54, kTextDim,
                 fontTitle());
        drawText("Choose a generated shape, load a .vscope path, or clear the path and click to draw.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        return;
    }
    drawText("Oscilloscope path", area.x + 20, area.y + 10, kAccent,
             fontBold());
    std::ostringstream info;
    info << m_scopePath.points.size() << " points  "
         << (m_scopePath.closed ? "closed" : "open") << "  "
         << kScopeShapeNames[m_scopeShape] << "  scan "
         << formatHz(m_scopeScanHz.value);
    drawText(info.str(), area.x + 20, area.y + 28, kTextFaint, fontSmall());

    const int availableW = area.w - 40;
    const int availableH = area.h - 78;
    const int side = std::clamp(std::min(availableW, availableH), 160,
                                std::max(160, availableH));
    SDL_Rect rect{area.x + (area.w - side) / 2, area.y + 46, side, side};
    m_scopeArea = rect;
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
    for (int i = 1; i < 8; ++i) {
        const int x = rect.x + rect.w * i / 8;
        const int y = rect.y + rect.h * i / 8;
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        SDL_RenderDrawLine(m_renderer, rect.x, y, rect.x + rect.w, y);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 210);
    SDL_RenderDrawLine(m_renderer, rect.x, rect.y + rect.h / 2,
                       rect.x + rect.w, rect.y + rect.h / 2);
    SDL_RenderDrawLine(m_renderer, rect.x + rect.w / 2, rect.y,
                       rect.x + rect.w / 2, rect.y + rect.h);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);

    auto mapPoint = [&](const vwave::ScopePoint& point) {
        const double x = std::clamp(point.x, -1.0, 1.0);
        const double y = std::clamp(point.y, -1.0, 1.0);
        return SDL_Point{
            rect.x + static_cast<int>(std::llround((x + 1.0) * 0.5 * rect.w)),
            rect.y + static_cast<int>(std::llround((1.0 - y) * 0.5 * rect.h))};
    };
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 240);
    for (size_t i = 1; i < m_scopePath.points.size(); ++i) {
        const SDL_Point a = mapPoint(m_scopePath.points[i - 1]);
        const SDL_Point b = mapPoint(m_scopePath.points[i]);
        SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
    }
    if (m_scopePath.closed && m_scopePath.points.size() > 2) {
        const SDL_Point a = mapPoint(m_scopePath.points.back());
        const SDL_Point b = mapPoint(m_scopePath.points.front());
        SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
    }
    if (m_scopeShape == 0) {
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               230);
        for (const auto& point : m_scopePath.points) {
            const SDL_Point p = mapPoint(point);
            SDL_Rect dot{p.x - 2, p.y - 2, 4, 4};
            SDL_RenderFillRect(m_renderer, &dot);
        }
    }
    drawText("x", rect.x + rect.w + 6, rect.y + rect.h / 2 - 6, kTextFaint,
             fontSmall());
    drawText("y", rect.x + rect.w / 2 - 3, rect.y - 16, kTextFaint,
             fontSmall());
    if (m_scopeShape == 0) {
        drawText("Click to add points; click near the first point to close.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
    } else {
        drawText("Generated path. Choose Polyline to draw by hand.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
    }
}

