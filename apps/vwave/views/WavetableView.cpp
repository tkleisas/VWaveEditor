#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vwave::ui;

void App::drawWavetable(const SDL_Rect& area) {
    m_wavetableArea = {area.x + 10, area.y + 30, area.w - 20,
                       static_cast<int>(area.h * 0.66)};
    m_frameStripArea = {area.x + 10,
                        m_wavetableArea.y + m_wavetableArea.h + 10,
                        area.w - 20,
                        area.h - m_wavetableArea.h - 50};
    if (!m_wavetableController.table().valid()) {
        drawText("No wavetable loaded", m_wavetableArea.x + 20,
                 m_wavetableArea.y + 20, kTextDim, fontNormal());
        drawText("Select audio and press Extract, or use the CLI/Lua API.",
                 m_wavetableArea.x + 20, m_wavetableArea.y + 44, kTextFaint,
                 fontSmall());
        return;
    }
    m_wavetableController.view().clamp(m_wavetableController.table().frameCount());
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &m_wavetableArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_wavetableArea);

    const int center = m_wavetableArea.y + m_wavetableArea.h / 2;
    for (int i = 0; i <= 8; ++i) {
        const int x = m_wavetableArea.x + m_wavetableArea.w * i / 8;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 55);
        SDL_RenderDrawLine(m_renderer, x, m_wavetableArea.y, x,
                           m_wavetableArea.y + m_wavetableArea.h);
        const int y = m_wavetableArea.y + m_wavetableArea.h * i / 8;
        SDL_RenderDrawLine(m_renderer, m_wavetableArea.x, y,
                           m_wavetableArea.x + m_wavetableArea.w, y);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 130);
    SDL_RenderDrawLine(m_renderer, m_wavetableArea.x, center,
                       m_wavetableArea.x + m_wavetableArea.w, center);

    const std::vector<float>& frame =
        m_wavetableController.table().frame(m_wavetableController.view().selectedFrame);
    const int frameSize = static_cast<int>(frame.size());
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 240);
    for (int x = 0; x < m_wavetableArea.w; ++x) {
        const int index = static_cast<int>(
            static_cast<double>(x) / std::max(1, m_wavetableArea.w - 1) *
            static_cast<double>(frameSize - 1));
        const float value = frame[static_cast<size_t>(
            std::clamp(index, 0, frameSize - 1))];
        const int y = center - static_cast<int>(
            value * (m_wavetableArea.h / 2 - 8));
        SDL_RenderDrawLine(m_renderer, m_wavetableArea.x + x, center,
                           m_wavetableArea.x + x, y);
    }

    std::ostringstream info;
    info << "Frame " << (m_wavetableController.view().selectedFrame + 1) << '/'
         << m_wavetableController.table().frameCount() << "   " << frameSize << " samples   "
         << "Drag to draw";
    drawText(info.str(), m_wavetableArea.x + 8, m_wavetableArea.y + 8,
             kTextDim, fontSmall());

    SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                           kPanelRaised.b, 255);
    SDL_RenderFillRect(m_renderer, &m_frameStripArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_frameStripArea);
    const int cellWidth = std::max(
        2, m_frameStripArea.w / std::max(1, m_wavetableController.table().frameCount()));
    const int stripCenter = m_frameStripArea.y + m_frameStripArea.h / 2;
    for (int frameIndex = 0; frameIndex < m_wavetableController.table().frameCount();
         ++frameIndex) {
        const int x = m_frameStripArea.x + frameIndex * cellWidth;
        if (x >= m_frameStripArea.x + m_frameStripArea.w) break;
        if (frameIndex == m_wavetableController.view().selectedFrame) {
            SDL_SetRenderDrawColor(m_renderer, kAccentDark.r, kAccentDark.g,
                                   kAccentDark.b, 255);
            SDL_Rect highlight{x, m_frameStripArea.y + 2,
                               std::max(1, cellWidth - 1),
                               m_frameStripArea.h - 4};
            SDL_RenderFillRect(m_renderer, &highlight);
        }
        SDL_SetRenderDrawColor(m_renderer, kTextDim.r, kTextDim.g, kTextDim.b,
                               frameIndex == m_wavetableController.view().selectedFrame
                                   ? 255
                                   : 130);
        const auto& data = m_wavetableController.table().frame(frameIndex);
        for (int sub = 0; sub < cellWidth && x + sub < m_frameStripArea.x +
                                                       m_frameStripArea.w;
             ++sub) {
            const size_t index = static_cast<size_t>(
                static_cast<double>(sub) / std::max(1, cellWidth) *
                static_cast<double>(data.size() - 1));
            const int y = stripCenter - static_cast<int>(
                data[index] * (m_frameStripArea.h / 2 - 4));
            SDL_RenderDrawPoint(m_renderer, x + sub, y);
        }
    }
}

