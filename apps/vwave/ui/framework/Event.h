#pragma once

#include "ui/framework/Geometry.h"

#include <string>
#include <variant>

namespace vwave::ui {

enum class PointerAction {
    Down,
    Up,
    Move,
};

struct PointerEvent {
    PointerAction action = PointerAction::Move;
    Point position;
    int button = 1;
};

enum class Key {
    Unknown,
    Tab,
    Space,
    Escape,
    Enter,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
};

struct KeyEvent {
    Key key = Key::Unknown;
    bool down = true;
    bool repeat = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

struct TextInputEvent {
    std::string text;
};

struct WindowEvent {
    enum class Type {
        Leave,
        FocusLost,
    };

    Type type = Type::Leave;
};

using Event = std::variant<PointerEvent, KeyEvent, TextInputEvent, WindowEvent>;

} // namespace vwave::ui
