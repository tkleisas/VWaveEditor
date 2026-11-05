#pragma once

#include "ui/framework/Event.h"
#include "ui/framework/Widget.h"

#include <vector>

namespace vwave::ui {

class WidgetHost {
public:
    void setRoot(Widget* root);
    Widget* root() const { return m_root; }

    bool handleEvent(const Event& event);

    Widget* hovered() const { return m_hovered; }
    Widget* captured() const { return m_captured; }
    Widget* focused() const { return m_focused; }
    CursorShape cursor() const { return m_cursor; }

    void setFocus(Widget* widget);
    void clearFocus() { setFocus(nullptr); }

private:
    void updateHovered(Widget* widget);
    void clearCapture();
    void focusNext(bool backward);
    void collectFocusable(Widget* widget, std::vector<Widget*>& out) const;

    Widget* m_root = nullptr;
    Widget* m_hovered = nullptr;
    Widget* m_captured = nullptr;
    Widget* m_focused = nullptr;
    CursorShape m_cursor = CursorShape::Arrow;
};

} // namespace vwave::ui
