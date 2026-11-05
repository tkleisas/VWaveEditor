#include "ui/sdl/SdlEventAdapter.h"

namespace vwave::ui {

namespace {

Key mapKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return Key::Tab;
    case SDLK_SPACE: return Key::Space;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_DELETE: return Key::Delete;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    default: return Key::Unknown;
    }
}

} // namespace

std::optional<Event> SdlEventAdapter::translate(const SDL_Event& event) const {
    switch (event.type) {
    case SDL_MOUSEMOTION:
        return PointerEvent{PointerAction::Move,
                            {event.motion.x, event.motion.y}, 0};
    case SDL_MOUSEBUTTONDOWN:
        return PointerEvent{PointerAction::Down,
                            {event.button.x, event.button.y},
                            event.button.button};
    case SDL_MOUSEBUTTONUP:
        return PointerEvent{PointerAction::Up,
                            {event.button.x, event.button.y},
                            event.button.button};
    case SDL_MOUSEWHEEL:
        return WheelEvent{{event.wheel.mouseX, event.wheel.mouseY},
                          event.wheel.preciseX, event.wheel.preciseY};
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        const SDL_Keysym& keysym = event.key.keysym;
        KeyEvent key;
        key.key = mapKey(keysym.sym);
        key.down = event.type == SDL_KEYDOWN;
        key.repeat = event.key.repeat != 0;
        key.ctrl = (keysym.mod & KMOD_CTRL) != 0;
        key.shift = (keysym.mod & KMOD_SHIFT) != 0;
        key.alt = (keysym.mod & KMOD_ALT) != 0;
        return key;
    }
    case SDL_TEXTINPUT:
        return TextInputEvent{event.text.text};
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_LEAVE)
            return WindowEvent{WindowEvent::Type::Leave};
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            return WindowEvent{WindowEvent::Type::FocusLost};
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

} // namespace vwave::ui
