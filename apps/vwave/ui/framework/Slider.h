#pragma once

#include "ui/framework/Widget.h"
#include "vwave/UiModel.h"

#include <functional>

namespace vwave::ui {

class Slider : public Widget {
public:
    vwave::ParameterSlider* slider = nullptr;
    std::function<void(double)> onChanged;

    bool focusable() const override { return enabled && slider; }
    CursorShape cursorAt(Point) const override {
        return enabled && slider ? CursorShape::SizeWE : CursorShape::Arrow;
    }
    bool onPointerDown(const PointerEvent& event) override;
    bool onPointerMove(const PointerEvent& event) override;
    bool onPointerUp(const PointerEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onPointerCaptureLost() override { dragging = false; }

    void setFromX(int x);

    bool dragging = false;
};

} // namespace vwave::ui
