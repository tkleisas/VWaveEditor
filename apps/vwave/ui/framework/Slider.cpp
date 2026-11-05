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

void Slider::paint(Painter& painter) {
    if (!slider) return;
    const int trackHeight = std::min(6, std::max(1, bounds.h));
    const Rect track{bounds.x, bounds.y + (bounds.h - trackHeight) / 2,
                     bounds.w, trackHeight};
    paintTrack(painter, track);
}

void Slider::paintTrack(Painter& painter, const Rect& track) {
    if (!slider) return;
    const ColorRole trackRole =
        enabled ? ColorRole::Content : ColorRole::Disabled;
    const ColorRole fillRole =
        enabled ? ColorRole::Accent : ColorRole::TextFaint;
    const ColorRole handleRole =
        enabled ? ColorRole::Text : ColorRole::TextFaint;
    painter.fillRect(track, trackRole);
    const int filled = static_cast<int>(
        std::lround(slider->normalized() * static_cast<double>(track.w)));
    painter.fillRect({track.x, track.y, filled, track.h}, fillRole);
    painter.strokeRect(track, ColorRole::Border);
    const int handleX = track.x + filled;
    painter.fillRect({handleX - 3, track.y - 4, 7, track.h + 8}, handleRole);
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
