#include "ui/framework/PushButton.h"

namespace vwave::ui {

bool PushButton::onPointerDown(const PointerEvent& event) {
    if (!enabled || event.button != 1) return false;
    pressed = true;
    return true;
}

bool PushButton::onPointerUp(const PointerEvent& event) {
    if (!pressed) return false;
    const bool inside = bounds.contains(event.position);
    pressed = false;
    if (inside && enabled) activate();
    return true;
}

bool PushButton::onKeyDown(const KeyEvent& event) {
    if (!enabled) return false;
    if (event.key != Key::Space && event.key != Key::Enter) return false;
    activate();
    return true;
}

void PushButton::activate() {
    if (enabled && onClick) onClick();
}

void PushButton::paint(Painter& painter) {
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
