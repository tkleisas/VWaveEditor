#include "ui/framework/Button.h"

namespace vwave::ui {

bool Button::onPointerDown(const PointerEvent& event) {
    if (!enabled || event.button != 1) return false;
    pressed = true;
    return true;
}

bool Button::onPointerUp(const PointerEvent& event) {
    if (!pressed) return false;
    const bool inside = bounds.contains(event.position);
    pressed = false;
    if (inside && enabled) activate();
    return true;
}

bool Button::onKeyDown(const KeyEvent& event) {
    if (!enabled) return false;
    if (event.key != Key::Space && event.key != Key::Enter) return false;
    activate();
    return true;
}

void Button::activate() {
    if (enabled && onClick) onClick();
}

void Button::paint(Painter& painter) {
    ColorRole fill = ColorRole::PanelRaised;
    if (!enabled) fill = ColorRole::Disabled;
    else if (accent || active || pressed) fill = ColorRole::AccentDark;
    painter.fillRect(bounds, fill);
    painter.strokeRect(bounds, enabled ? ColorRole::Border
                                       : ColorRole::TextFaint);
    const ColorRole text =
        !enabled ? ColorRole::TextFaint
                 : ((accent || active) ? ColorRole::AccentText
                                       : ColorRole::Text);
    painter.drawText(label, bounds, text, FontRole::Small, TextAlign::Center);
}

} // namespace vwave::ui
