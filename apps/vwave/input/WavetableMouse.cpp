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

void App::handleWavetableMouseMotion(const SDL_MouseMotionEvent& event) {
if (m_drawingWavetable && m_wavetableController.valid()) {
    const int64_t index = static_cast<int64_t>(
        static_cast<double>(event.x - m_wavetableArea.x) /
        std::max(1, m_wavetableArea.w) *
        (m_wavetableController.table().frameSize() - 1));
    const float value = std::clamp(
        1.0f - 2.0f * static_cast<float>(
                          event.y - m_wavetableArea.y) /
                          std::max(1, m_wavetableArea.h),
        -1.0f, 1.0f);
    auto& frame = m_wavetableController.table().frame(m_wavetableController.view().selectedFrame);
    const int clampedIndex = std::clamp(
        static_cast<int>(index), 0,
        static_cast<int>(frame.size()) - 1);
    if (m_lastWavetableSample >= 0 &&
        clampedIndex != m_lastWavetableSample) {
        const int start = std::min(m_lastWavetableSample, clampedIndex);
        const int end = std::max(m_lastWavetableSample, clampedIndex);
        const float startValue = frame[static_cast<size_t>(start)];
        for (int i = start; i <= end; ++i) {
            const float mix = static_cast<float>(i - start) /
                              std::max(1, end - start);
            frame[static_cast<size_t>(i)] =
                startValue + (value - startValue) * mix;
        }
    } else {
        frame[static_cast<size_t>(clampedIndex)] = value;
    }
    m_lastWavetableSample = clampedIndex;
}
}
