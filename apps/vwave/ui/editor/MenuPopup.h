#pragma once

#include "ui/framework/Painter.h"
#include "ui/framework/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace vwave::ui {

struct PopupItem {
    std::string label;
    std::string shortcut;
    std::function<void()> action;
    bool enabled = true;
    bool separator = false;
};

class MenuPopup : public Widget {
public:
    void setItems(std::vector<PopupItem> items);

    CursorShape cursorAt(Point point) const override;
    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerMove(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    void onPointerCaptureLost() override;
    void onHoverChanged(bool state) override;

protected:
    void paint(Painter& painter) override;

private:
    int itemAt(Point point) const;

    std::vector<PopupItem> m_items;
    std::vector<Rect> m_itemRects;
    int m_hovered = -1;
    int m_pressed = -1;
    bool m_hoveredWidget = false;
};

} // namespace vwave::ui
