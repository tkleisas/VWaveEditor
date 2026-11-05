#include "ui/editor/SidebarSlider.h"

#include <algorithm>

namespace vwave::ui {

void SidebarSlider::paint(Painter& painter) {
    if (!slider) return;
    const int valueWidth = painter.textWidth(valueText, FontRole::Small);
    painter.drawText(label,
                     {bounds.x, bounds.y,
                      std::max(0, bounds.w - valueWidth - 8), 14},
                     enabled ? ColorRole::TextDim : ColorRole::TextFaint,
                     FontRole::Small);
    painter.drawText(valueText,
                     {bounds.x + std::max(0, bounds.w - valueWidth),
                      bounds.y, valueWidth, 14},
                     enabled ? ColorRole::Text : ColorRole::TextFaint,
                     FontRole::Small, TextAlign::End);
    paintTrack(painter, {bounds.x, bounds.y + 17, bounds.w, 6});
}

} // namespace vwave::ui
