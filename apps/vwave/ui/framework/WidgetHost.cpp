#include "ui/framework/WidgetHost.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace vwave::ui {

namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

void WidgetHost::setRoot(Widget* root) {
    clearCapture();
    updateHovered(nullptr);
    setFocus(nullptr);
    m_root = root;
}

void WidgetHost::setFocus(Widget* widget) {
    if (widget && (!widget->visible || !widget->enabled ||
                   !widget->focusable()))
        return;
    if (m_focused == widget) return;
    if (m_focused) m_focused->onFocusChanged(false);
    m_focused = widget;
    if (m_focused) m_focused->onFocusChanged(true);
}

void WidgetHost::collectFocusable(Widget* widget,
                                  std::vector<Widget*>& out) const {
    if (!widget || !widget->visible || !widget->enabled) return;
    if (widget->focusable()) out.push_back(widget);
    for (const auto& child : widget->children()) {
        if (!child->visible || !child->enabled) continue;
        collectFocusable(child.get(), out);
    }
}

void WidgetHost::focusNext(bool backward) {
    std::vector<Widget*> focusable;
    collectFocusable(m_root, focusable);
    if (focusable.empty()) {
        setFocus(nullptr);
        return;
    }
    auto current = std::find(focusable.begin(), focusable.end(), m_focused);
    if (current == focusable.end()) {
        setFocus(backward ? focusable.back() : focusable.front());
        return;
    }
    if (backward) {
        if (current == focusable.begin()) current = focusable.end();
        setFocus(*(--current));
    } else {
        ++current;
        if (current == focusable.end()) current = focusable.begin();
        setFocus(*current);
    }
}

void WidgetHost::updateHovered(Widget* widget) {
    if (m_hovered == widget) return;
    if (m_hovered) m_hovered->onHoverChanged(false);
    m_hovered = widget;
    if (m_hovered) m_hovered->onHoverChanged(true);
}

void WidgetHost::clearCapture() {
    if (m_captured) {
        m_captured->onPointerCaptureLost();
        m_captured = nullptr;
    }
}

bool WidgetHost::handleEvent(const Event& event) {
    return std::visit(
        Overloaded{
            [this](const PointerEvent& pointer) {
                if (pointer.action == PointerAction::Down) {
                    Widget* target =
                        m_root ? m_root->hitTest(pointer.position) : nullptr;
                    if (!target) {
                        updateHovered(nullptr);
                        m_cursor = CursorShape::Arrow;
                        return false;
                    }
                    updateHovered(target);
                    m_cursor = target->cursorAt(pointer.position);
                    if (target->focusable()) setFocus(target);
                    if (target->onPointerDown(pointer)) {
                        m_captured = target;
                    } else {
                        m_captured = nullptr;
                    }
                    return true;
                }
                if (pointer.action == PointerAction::Move) {
                    if (m_captured) {
                        m_captured->onPointerMove(pointer);
                        m_cursor = m_captured->cursorAt(pointer.position);
                        return true;
                    }
                    Widget* target =
                        m_root ? m_root->hitTest(pointer.position) : nullptr;
                    updateHovered(target);
                    m_cursor = target ? target->cursorAt(pointer.position)
                                      : CursorShape::Arrow;
                    if (target) target->onPointerMove(pointer);
                    return target != nullptr;
                }
                if (pointer.action == PointerAction::Up) {
                    Widget* target = nullptr;
                    if (m_captured) {
                        target = m_captured;
                        m_captured = nullptr;
                    } else if (m_root) {
                        target = m_root->hitTest(pointer.position);
                    }
                    if (target) target->onPointerUp(pointer);
                    Widget* hover =
                        m_root ? m_root->hitTest(pointer.position) : nullptr;
                    updateHovered(hover);
                    m_cursor = hover ? hover->cursorAt(pointer.position)
                                     : CursorShape::Arrow;
                    return target != nullptr;
                }
                return false;
            },
            [this](const KeyEvent& key) {
                if (!key.down) {
                    return m_focused ? m_focused->onKeyUp(key) : false;
                }
                if (key.key == Key::Tab && !key.ctrl && !key.alt) {
                    focusNext(key.shift);
                    return true;
                }
                return m_focused ? m_focused->onKeyDown(key) : false;
            },
            [this](const TextInputEvent& text) {
                return m_focused ? m_focused->onTextInput(text) : false;
            },
            [this](const WindowEvent& window) {
                if (window.type == WindowEvent::Type::FocusLost) {
                    clearCapture();
                    updateHovered(nullptr);
                    m_cursor = CursorShape::Arrow;
                    return true;
                }
                updateHovered(nullptr);
                m_cursor = CursorShape::Arrow;
                return true;
            },
        },
        event);
}

} // namespace vwave::ui
