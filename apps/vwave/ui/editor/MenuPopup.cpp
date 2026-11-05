#include "ui/editor/MenuPopup.h"

#include <utility>

namespace vwave::ui {

void MenuPopup::setItems(std::vector<PopupItem> items) {
    m_items = std::move(items);
    m_itemRects.clear();
    m_itemRects.reserve(m_items.size());
    int y = bounds.y + 8;
    for (const auto& item : m_items) {
        const int height = item.separator ? 7 : 24;
        m_itemRects.push_back(
            item.separator ? Rect{} : Rect{bounds.x + 1, y, bounds.w - 2, 24});
        y += height;
    }
    if (m_hovered >= static_cast<int>(m_items.size())) m_hovered = -1;
    if (m_pressed >= static_cast<int>(m_items.size())) m_pressed = -1;
}

int MenuPopup::itemAt(Point point) const {
    for (size_t i = 0; i < m_itemRects.size(); ++i) {
        if (m_itemRects[i].w > 0 && m_itemRects[i].contains(point))
            return static_cast<int>(i);
    }
    return -1;
}

CursorShape MenuPopup::cursorAt(Point point) const {
    const int index = itemAt(point);
    if (index < 0 || index >= static_cast<int>(m_items.size()))
        return CursorShape::Arrow;
    const auto& item = m_items[static_cast<size_t>(index)];
    return item.enabled && !item.separator ? CursorShape::Hand
                                           : CursorShape::Arrow;
}

bool MenuPopup::onPointerDown(const PointerEvent& event) {
    if (event.button != 1) return false;
    m_pressed = itemAt(event.position);
    if (m_pressed < 0) return bounds.contains(event.position);
    const auto& item = m_items[static_cast<size_t>(m_pressed)];
    if (!item.enabled || item.separator) {
        m_pressed = -1;
        return true;
    }
    return true;
}

bool MenuPopup::onPointerMove(const PointerEvent& event) {
    const int index = itemAt(event.position);
    const bool enabled =
        index >= 0 && index < static_cast<int>(m_items.size()) &&
        m_items[static_cast<size_t>(index)].enabled &&
        !m_items[static_cast<size_t>(index)].separator;
    m_hovered = enabled ? index : -1;
    return bounds.contains(event.position);
}

bool MenuPopup::onPointerUp(const PointerEvent& event) {
    const int index = itemAt(event.position);
    const int pressed = m_pressed;
    m_pressed = -1;
    if (pressed < 0 || pressed != index) return true;
    const auto& item = m_items[static_cast<size_t>(index)];
    if (item.enabled && !item.separator && item.action) item.action();
    return true;
}

void MenuPopup::onPointerCaptureLost() { m_pressed = -1; }

void MenuPopup::onHoverChanged(bool state) {
    m_hoveredWidget = state;
    if (!state) m_hovered = -1;
}

void MenuPopup::paint(Painter& painter) {
    painter.fillRect(bounds, ColorRole::Panel);
    painter.strokeRect(bounds, ColorRole::Border);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];
        const Rect& row = m_itemRects[i];
        if (item.separator) {
            const int y = bounds.y + 8;
            int offset = 0;
            for (size_t j = 0; j < i; ++j)
                offset += m_items[j].separator ? 7 : 24;
            painter.drawLine({bounds.x + 8, y + offset + 3},
                             {bounds.x + bounds.w - 8, y + offset + 3},
                             ColorRole::Border);
            continue;
        }
        const int index = static_cast<int>(i);
        if (item.enabled && (index == m_hovered || index == m_pressed))
            painter.fillRect(row, ColorRole::AccentDark);
        const ColorRole text =
            item.enabled ? ColorRole::Text : ColorRole::TextFaint;
        painter.drawText(item.label, {row.x + 12, row.y, row.w - 24, row.h},
                         text, FontRole::Normal);
        if (!item.shortcut.empty()) {
            const int width = painter.textWidth(item.shortcut, FontRole::Small);
            painter.drawText(item.shortcut,
                             {row.x + row.w - width - 10, row.y, width, row.h},
                             ColorRole::TextFaint, FontRole::Small,
                             TextAlign::End);
        }
    }
}

} // namespace vwave::ui
