#include "test_framework.h"

#include "ui/sdl/SdlCursorController.h"
#include "ui/sdl/SdlEventAdapter.h"

#include <SDL.h>

using namespace vwave::ui;

VWTEST("SDL adapter translates pointer events") {
    SdlEventAdapter adapter;
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 12;
    event.motion.y = 34;
    const auto move = adapter.translate(event);
    REQUIRE(move.has_value());
    const auto* pointer = std::get_if<PointerEvent>(&*move);
    REQUIRE(pointer != nullptr);
    CHECK(pointer->action == PointerAction::Move);
    CHECK_EQ(pointer->position.x, 12);
    CHECK_EQ(pointer->position.y, 34);

    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.x = 5;
    event.button.y = 6;
    event.button.button = SDL_BUTTON_RIGHT;
    const auto down = adapter.translate(event);
    REQUIRE(down.has_value());
    pointer = std::get_if<PointerEvent>(&*down);
    REQUIRE(pointer != nullptr);
    CHECK(pointer->action == PointerAction::Down);
    CHECK_EQ(pointer->button, int{SDL_BUTTON_RIGHT});
}

VWTEST("SDL adapter translates wheel and text events") {
    SdlEventAdapter adapter;
    SDL_Event event{};
    event.type = SDL_MOUSEWHEEL;
    event.wheel.mouseX = 7;
    event.wheel.mouseY = 9;
    event.wheel.preciseX = 1.5;
    event.wheel.preciseY = -2.5;
    const auto wheel = adapter.translate(event);
    REQUIRE(wheel.has_value());
    const auto* wheelEvent = std::get_if<WheelEvent>(&*wheel);
    REQUIRE(wheelEvent != nullptr);
    CHECK_EQ(wheelEvent->position.x, 7);
    CHECK_EQ(wheelEvent->position.y, 9);
    CHECK_NEAR(wheelEvent->deltaX, 1.5, 1e-9);
    CHECK_NEAR(wheelEvent->deltaY, -2.5, 1e-9);

    event = SDL_Event{};
    event.type = SDL_TEXTINPUT;
    std::snprintf(event.text.text, sizeof(event.text.text), "%s", "abc");
    const auto text = adapter.translate(event);
    REQUIRE(text.has_value());
    const auto* textEvent = std::get_if<TextInputEvent>(&*text);
    REQUIRE(textEvent != nullptr);
    CHECK(textEvent->text == "abc");
}

VWTEST("SDL adapter translates keys and modifiers") {
    SdlEventAdapter adapter;
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_TAB;
    event.key.keysym.mod = KMOD_LSHIFT;
    event.key.repeat = 1;
    const auto translated = adapter.translate(event);
    REQUIRE(translated.has_value());
    const auto* key = std::get_if<KeyEvent>(&*translated);
    REQUIRE(key != nullptr);
    CHECK(key->key == Key::Tab);
    CHECK(key->down);
    CHECK(key->repeat);
    CHECK(key->shift);
    CHECK(!key->ctrl);
}

VWTEST("SDL adapter translates window leave and focus loss") {
    SdlEventAdapter adapter;
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_LEAVE;
    const auto leave = adapter.translate(event);
    REQUIRE(leave.has_value());
    const auto* leaveEvent = std::get_if<WindowEvent>(&*leave);
    REQUIRE(leaveEvent != nullptr);
    CHECK(leaveEvent->type == WindowEvent::Type::Leave);

    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    const auto focusLost = adapter.translate(event);
    REQUIRE(focusLost.has_value());
    const auto* focusEvent = std::get_if<WindowEvent>(&*focusLost);
    REQUIRE(focusEvent != nullptr);
    CHECK(focusEvent->type == WindowEvent::Type::FocusLost);
}

VWTEST("SDL adapter ignores unrelated events") {
    SdlEventAdapter adapter;
    SDL_Event event{};
    event.type = SDL_QUIT;
    CHECK(!adapter.translate(event).has_value());
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_SHOWN;
    CHECK(!adapter.translate(event).has_value());
}

VWTEST("SDL cursor shapes map to system cursors") {
    CHECK(systemCursorFor(CursorShape::Arrow) == SDL_SYSTEM_CURSOR_ARROW);
    CHECK(systemCursorFor(CursorShape::Hand) == SDL_SYSTEM_CURSOR_HAND);
    CHECK(systemCursorFor(CursorShape::OpenHand) == SDL_SYSTEM_CURSOR_HAND);
    CHECK(systemCursorFor(CursorShape::ClosedHand) == SDL_SYSTEM_CURSOR_HAND);
    CHECK(systemCursorFor(CursorShape::SizeWE) == SDL_SYSTEM_CURSOR_SIZEWE);
    CHECK(systemCursorFor(CursorShape::SizeNS) == SDL_SYSTEM_CURSOR_SIZENS);
    CHECK(systemCursorFor(CursorShape::SizeAll) == SDL_SYSTEM_CURSOR_SIZEALL);
    CHECK(systemCursorFor(CursorShape::Crosshair) ==
          SDL_SYSTEM_CURSOR_CROSSHAIR);
}
