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

void App::handleWavetableMouseDown(const SDL_MouseButtonEvent& event) {
    SDL_Point point{event.x, event.y};
    if (SDL_PointInRect(&point, &m_frameStripArea) &&
        m_wavetableController.valid()) {
        const int cellWidth = std::max(
            2, m_frameStripArea.w / std::max(1, m_wavetableController.table().frameCount()));
        const int index = (event.x - m_frameStripArea.x) / cellWidth;
        m_wavetableController.view().selectedFrame = index;
        m_wavetableController.view().clamp(m_wavetableController.table().frameCount());
    } else if (SDL_PointInRect(&point, &m_wavetableArea) &&
               m_wavetableController.valid()) {
        m_drawingWavetable = true;
        m_lastWavetableSample = -1;
    }

}
