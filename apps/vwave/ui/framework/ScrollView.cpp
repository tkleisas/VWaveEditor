#include "ui/framework/ScrollView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vwave::ui {

Widget& ScrollView::setContent(std::unique_ptr<Widget> content) {
    m_content = content.get();
    return addChild(std::move(content));
}

int ScrollView::maxScroll() const {
    return std::max(0, contentHeight - bounds.h);
}

void ScrollView::setScrollOffset(int offset) {
    const int clamped = std::clamp(offset, 0, maxScroll());
    const int delta = clamped - m_offset;
    m_offset = clamped;
    if (m_content && delta != 0) m_content->translate({0, -delta});
}

bool ScrollView::onWheel(const WheelEvent& event) {
    if (contentHeight <= bounds.h || event.deltaY == 0.0) return false;
    const int before = m_offset;
    const int delta = static_cast<int>(
        std::lround(-event.deltaY * static_cast<double>(wheelStep)));
    setScrollOffset(m_offset + delta);
    return m_offset != before;
}

void ScrollView::draw(Painter& painter) {
    if (!visible) return;
    painter.pushClip(bounds);
    paint(painter);
    if (m_content) m_content->draw(painter);
    painter.popClip();
}

} // namespace vwave::ui
