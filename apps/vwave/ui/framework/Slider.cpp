#include "ui/framework/Slider.h"

#include <algorithm>
#include <cmath>

namespace vwave::ui {

void Slider::setFromX(int x) {
    if (!slider || bounds.w <= 0) return;
    const double position = std::clamp(
        static_cast<double>(x - bounds.x) / static_cast<double>(bounds.w),
        0.0, 1.0);
    slider->setNormalized(position);
    if (onChanged) onChanged(slider->value);
}

bool Slider::onPointerDown(const PointerEvent& event) {
    if (!enabled || !slider || event.button != 1) return false;
    dragging = true;
    setFromX(event.position.x);
    return true;
}

bool Slider::onPointerMove(const PointerEvent& event) {
    if (!dragging) return false;
    setFromX(event.position.x);
    return true;
}

bool Slider::onPointerUp(const PointerEvent&) {
    if (!dragging) return false;
    dragging = false;
    return true;
}

bool Slider::onKeyDown(const KeyEvent& event) {
    if (!enabled || !slider) return false;
    const double range = slider->maximum - slider->minimum;
    const double step = slider->step > 0.0 ? slider->step : range / 100.0;
    double value = slider->value;
    switch (event.key) {
    case Key::Left:
    case Key::Down:
        value -= step;
        break;
    case Key::Right:
    case Key::Up:
        value += step;
        break;
    case Key::Home:
        value = slider->minimum;
        break;
    case Key::End:
        value = slider->maximum;
        break;
    default:
        return false;
    }
    slider->setValue(value);
    if (onChanged) onChanged(slider->value);
    return true;
}

} // namespace vwave::ui
