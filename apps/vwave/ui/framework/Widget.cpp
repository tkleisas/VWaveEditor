#include "ui/framework/Widget.h"

#include <utility>

namespace vwave::ui {

Widget& Widget::addChild(std::unique_ptr<Widget> child) {
    if (!child) return *this;
    child->parent = this;
    m_children.push_back(std::move(child));
    return *m_children.back();
}

Widget* Widget::hitTest(Point point) {
    if (!visible || !bounds.contains(point)) return nullptr;
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if (Widget* hit = (*it)->hitTest(point)) return hit;
    }
    return enabled ? this : nullptr;
}

} // namespace vwave::ui
