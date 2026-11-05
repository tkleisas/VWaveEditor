#include "ui/framework/Layout.h"

#include <algorithm>

namespace vwave::ui {

int layoutColumn(Widget& parent, Rect area, int spacing, int inset) {
    const int x = area.x + inset;
    const int width = std::max(0, area.w - inset * 2);
    int cursor = area.y + inset;
    bool first = true;
    for (const auto& child : parent.children()) {
        if (!child->visible) continue;
        if (!first) cursor += spacing;
        child->bounds.x = x;
        child->bounds.y = cursor;
        child->bounds.w = width;
        cursor += child->bounds.h;
        first = false;
    }
    return cursor - area.y + inset;
}

int layoutRow(Widget& parent, Rect area, int spacing, int inset) {
    const int y = area.y + inset;
    const int height = std::max(0, area.h - inset * 2);
    int cursor = area.x + inset;
    bool first = true;
    for (const auto& child : parent.children()) {
        if (!child->visible) continue;
        if (!first) cursor += spacing;
        child->bounds.x = cursor;
        child->bounds.y = y;
        child->bounds.h = height;
        cursor += child->bounds.w;
        first = false;
    }
    return cursor - area.x + inset;
}

void layoutStack(Widget& parent, Rect area, int inset) {
    const Rect target{area.x + inset, area.y + inset,
                      std::max(0, area.w - inset * 2),
                      std::max(0, area.h - inset * 2)};
    for (const auto& child : parent.children()) {
        if (!child->visible) continue;
        child->bounds = target;
    }
}

} // namespace vwave::ui
