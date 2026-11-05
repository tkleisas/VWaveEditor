#pragma once

#include "ui/framework/Widget.h"

#include <functional>
#include <string>

namespace vwave::ui {

class Button : public Widget {
public:
    std::string label;
    std::function<void()> onClick;

    bool focusable() const override { return enabled; }
    CursorShape cursorAt(Point) const override {
        return enabled ? CursorShape::Hand : CursorShape::Arrow;
    }
    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onHoverChanged(bool state) override { hovered = state; }
    void onPointerCaptureLost() override { pressed = false; }
    void activate();

    bool hovered = false;
    bool pressed = false;
};

} // namespace vwave::ui
