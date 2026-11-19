#include "App.h"

#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace vwave::ui;

void App::drawXenakis(const SDL_Rect& area) {
    if (!m_xenakisController.hasPage()) {
        drawText("No Xenakis page", area.x + 40, area.y + 54, kTextDim,
                 fontTitle());
        drawText("Generate a page in the sidebar or open a .xenpage file.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        m_xenakisArea = {};
        return;
    }

    const xen::Page& page = m_xenakisController.page();
    const double duration = std::max(0.001, page.duration);
    std::ostringstream header;
    header << m_xenakisController.pageName() << "  "
           << formatSeconds(duration) << "  " << page.arcs.size() << " arcs";
    drawText(header.str(), area.x + 20, area.y + 10, kAccent, fontBold());

    SDL_Rect rect{area.x + 20, area.y + 46, area.w - 40, area.h - 112};
    if (rect.w <= 0 || rect.h <= 0) return;
    m_xenakisArea = rect;
    const double viewStart = xenakisViewStart();
    const double viewDuration = xenakisViewDuration();
    const auto xForTime = [&](double time) {
        return rect.x + static_cast<int>(std::llround(
                            (time - viewStart) / viewDuration * rect.w));
    };

    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);

    double step = 1.0;
    const double targets[] = {0.05, 0.1, 0.25, 0.5, 1.0, 2.0,
                              5.0,  10.0, 15.0, 30.0, 60.0};
    for (double target : targets) {
        step = target;
        if (viewDuration / target <= 10.0) break;
    }

    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
    const double firstTime = std::floor(viewStart / step) * step;
    for (double t = firstTime;
         t <= viewStart + viewDuration + 1e-9; t += step) {
        const int x = xForTime(t);
        if (x < rect.x || x > rect.x + rect.w) continue;
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        if (x <= rect.x + rect.w - 40)
            drawText(formatSeconds(t), x + 3, rect.y + 2, kTextFaint,
                     fontSmall());
    }

    const xen::FrequencyTable* table = xenakisActiveTable();
    if (table != nullptr) {
        const double low = table->lowIndex;
        const double high = std::max(low + 1.0, table->highIndex);
        const double divisions =
            std::max(1.0, table->tuning.divisionsPerOctave);
        const double first = std::ceil(low / divisions) * divisions;
        int lines = 0;
        for (double index = first;
             index <= high && lines < 16; index += divisions, ++lines) {
            const double normalized = (index - low) / (high - low);
            const int y = rect.y + static_cast<int>(std::llround(
                                     (1.0 - normalized) * rect.h));
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 70);
            SDL_RenderDrawLine(m_renderer, rect.x, y, rect.x + rect.w, y);
            drawText(formatHz(table->tuning.indexToHz(index)), rect.x + 3,
                     y - 13, kTextFaint, fontSmall());
        }
    }

    const SDL_Color groupColors[4] = {
        kAccent, kOrange, SDL_Color{112, 206, 142, 255},
        SDL_Color{190, 142, 232, 255}};

    SDL_RenderSetClipRect(m_renderer, &rect);
    for (size_t i = 0; i < page.arcs.size(); ++i) {
        const xen::Arc& arc = page.arcs[i];
        if (arc.points.size() < 2) continue;
        const bool selected =
            static_cast<int>(i) == m_xenakisSelectedArc;
        SDL_Color color = groupColors[static_cast<size_t>(
            std::clamp(arc.group, 0, 3))];
        if (arc.muted) color.a = 70;
        if (selected) color = kText;

        for (size_t point = 1; point < arc.points.size(); ++point) {
            const double t0 = std::clamp<double>(
                arc.points[point - 1].time, 0.0, duration);
            const double t1 = std::clamp<double>(
                arc.points[point].time, 0.0, duration);
            const double p0 = std::clamp<double>(
                arc.points[point - 1].pitch, 0.0, 1.0);
            const double p1 = std::clamp<double>(
                arc.points[point].pitch, 0.0, 1.0);
            const int x0 = xForTime(t0);
            const int x1 = xForTime(t1);
            const int y0 = rect.y + static_cast<int>(
                                       std::llround((1.0 - p0) * rect.h));
            const int y1 = rect.y + static_cast<int>(
                                       std::llround((1.0 - p1) * rect.h));
            if (selected) {
                SDL_SetRenderDrawColor(m_renderer, kText.r, kText.g,
                                       kText.b, 90);
                SDL_RenderDrawLine(m_renderer, x0, y0 - 1, x1, y1 - 1);
                SDL_RenderDrawLine(m_renderer, x0, y0 + 1, x1, y1 + 1);
            }
            SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b,
                                   color.a);
            SDL_RenderDrawLine(m_renderer, x0, y0, x1, y1);
        }
    }

    if (m_xenakisSelectedArc >= 0 &&
        m_xenakisSelectedArc < static_cast<int>(page.arcs.size())) {
        const xen::Arc& arc =
            page.arcs[static_cast<size_t>(m_xenakisSelectedArc)];
        for (size_t i = 0; i < arc.points.size(); ++i) {
            const int x = xForTime(arc.points[i].time);
            const int y = rect.y + static_cast<int>(std::llround(
                                       (1.0 - std::clamp<double>(
                                                  arc.points[i].pitch, 0.0,
                                                  1.0)) *
                                       rect.h));
            const bool current =
                static_cast<int>(i) == m_xenakisSelectedPoint;
            if (current) {
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 255);
                SDL_Rect marker{x - 4, y - 4, 9, 9};
                SDL_RenderFillRect(m_renderer, &marker);
            } else {
                SDL_SetRenderDrawColor(m_renderer, kText.r, kText.g,
                                       kText.b, 220);
                SDL_Rect marker{x - 2, y - 2, 5, 5};
                SDL_RenderFillRect(m_renderer, &marker);
            }
        }
    }

    if (m_xenakisDrawing && m_xenakisFreehand &&
        m_xenakisFreehandPoints.size() >= 2) {
        std::vector<xen::ArcPoint> preview = m_xenakisFreehandPoints;
        preview.push_back(
            {m_xenakisDrawCurrentTime, m_xenakisDrawCurrentPitch});
        SDL_SetRenderDrawColor(m_renderer, kAccentText.r, kAccentText.g,
                               kAccentText.b, 255);
        for (size_t i = 1; i < preview.size(); ++i) {
            const int x0 = xForTime(preview[i - 1].time);
            const int y0 = rect.y + static_cast<int>(std::llround(
                                       (1.0 - preview[i - 1].pitch) *
                                       rect.h));
            const int x1 = xForTime(preview[i].time);
            const int y1 = rect.y + static_cast<int>(std::llround(
                                       (1.0 - preview[i].pitch) * rect.h));
            SDL_RenderDrawLine(m_renderer, x0, y0, x1, y1);
        }
    } else if (m_xenakisDrawing) {
        const int x0 = xForTime(m_xenakisDrawStartTime);
        const int x1 = xForTime(m_xenakisDrawCurrentTime);
        const int y0 = rect.y + static_cast<int>(std::llround(
                                   (1.0 - m_xenakisDrawStartPitch) * rect.h));
        const int y1 = rect.y + static_cast<int>(std::llround(
                                   (1.0 - m_xenakisDrawCurrentPitch) * rect.h));
        SDL_SetRenderDrawColor(m_renderer, kAccentText.r, kAccentText.g,
                               kAccentText.b, 255);
        SDL_RenderDrawLine(m_renderer, x0, y0, x1, y1);
        SDL_Rect start{x0 - 2, y0 - 2, 5, 5};
        SDL_Rect end{x1 - 2, y1 - 2, 5, 5};
        SDL_RenderFillRect(m_renderer, &start);
        SDL_RenderFillRect(m_renderer, &end);
    }
    if (m_xenakisController.previewing() &&
        m_xenakisController.previewSampleRate() > 0) {
        const double seconds =
            static_cast<double>(m_xenakisController.previewPosition()) /
            static_cast<double>(m_xenakisController.previewSampleRate());
        if (seconds >= viewStart &&
            seconds <= viewStart + viewDuration) {
            const int x = xForTime(seconds);
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                   kOrange.b, 255);
            SDL_RenderDrawLine(m_renderer, x, rect.y, x,
                               rect.y + rect.h);
            SDL_Rect marker{x - 3, rect.y, 7, 5};
            SDL_RenderFillRect(m_renderer, &marker);
        }
    }

    SDL_RenderSetClipRect(m_renderer, nullptr);

    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);

    if (page.arcs.empty() && !m_xenakisDrawing) {
        drawText("Draw an arc: click and drag across the canvas.",
                 rect.x + 16, rect.y + 16, kTextFaint, fontNormal());
    }

    static const char* kToolNames[] = {"Draw", "Select", "Erase"};
    std::ostringstream tool;
    tool << "Tool: " << kToolNames[std::clamp(m_xenakisTool, 0, 2)]
         << "  ·  snap pitch " << (m_xenakisSnapPitch ? "on" : "off")
         << "  ·  snap time " << (m_xenakisSnapTime ? "on" : "off")
         << "  ·  " << (m_xenakisController.canUndoPage() ? "undo ready"
                                                         : "no page undo");
    drawText(tool.str(), area.x + 20, area.y + area.h - 52, kTextFaint,
             fontSmall());
    drawText(m_xenakisController.previewing()
                 ? "Preview: click or drag the canvas to seek  \u00b7  "
                   "double-click a point to add  \u00b7  right-click to "
                   "delete"
                 : "Double-click an arc to add a point  \u00b7  "
                   "right-click a breakpoint to delete",
             area.x + 20, area.y + area.h - 68, kTextFaint, fontSmall());

    std::ostringstream info;
    info << "Source " << m_xenakisController.pageName() << "  ·  GENDYN "
         << (m_xenakisMirror ? "mirror" : "free") << "  ·  "
         << (m_xenakisNormalize ? "normalize" : "raw");
    if (m_xenakisViewDuration > 0.0) {
        info << "  ·  view " << formatSeconds(viewStart) << "-"
             << formatSeconds(viewStart + viewDuration);
    }
    drawText(info.str(), area.x + 20, area.y + area.h - 30, kTextDim,
             fontSmall());
}
