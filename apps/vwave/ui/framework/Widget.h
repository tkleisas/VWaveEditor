#pragma once

#include "ui/framework/Event.h"
#include "ui/framework/Geometry.h"

#include <memory>
#include <vector>

namespace vwave::ui {

class Widget {
public:
    virtual ~Widget() = default;

    Rect bounds;
    bool visible = true;
    bool enabled = true;

    Widget* parent = nullptr;
    const std::vector<std::unique_ptr<Widget>>& children() const {
        return m_children;
    }

    Widget& addChild(std::unique_ptr<Widget> child);
    Widget* hitTest(Point point);

    virtual CursorShape cursorAt(Point) const { return CursorShape::Arrow; }
    virtual bool focusable() const { return false; }
    virtual bool onPointerDown(const PointerEvent&) { return false; }
    virtual bool onPointerMove(const PointerEvent&) { return false; }
    virtual bool onPointerUp(const PointerEvent&) { return false; }
    virtual bool onWheel(const WheelEvent&) { return false; }
    virtual bool onKeyDown(const KeyEvent&) { return false; }
    virtual bool onKeyUp(const KeyEvent&) { return false; }
    virtual bool onTextInput(const TextInputEvent&) { return false; }
    virtual void onHoverChanged(bool) {}
    virtual void onFocusChanged(bool) {}
    virtual void onPointerCaptureLost() {}

private:
    std::vector<std::unique_ptr<Widget>> m_children;
};

} // namespace vwave::ui
