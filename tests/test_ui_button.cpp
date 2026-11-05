#include "test_framework.h"

#include "ui/framework/PushButton.h"
#include "ui/framework/WidgetHost.h"

#include <memory>

using namespace vwave::ui;

namespace {

KeyEvent keyDown(Key key) {
    KeyEvent event;
    event.key = key;
    event.down = true;
    return event;
}

} // namespace

VWTEST("button click activates on release inside") {
    PushButton button;
    button.bounds = {10, 10, 40, 20};
    button.label = "Apply";
    int clicks = 0;
    button.onClick = [&] { ++clicks; };

    WidgetHost host;
    host.setRoot(&button);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {15, 15}, 1}));
    CHECK(button.pressed);
    CHECK_EQ(clicks, 0);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {20, 20}, 1}));
    CHECK(!button.pressed);
    CHECK_EQ(clicks, 1);
}

VWTEST("button release outside or cancellation does not activate") {
    PushButton button;
    button.bounds = {10, 10, 40, 20};
    int clicks = 0;
    button.onClick = [&] { ++clicks; };

    WidgetHost host;
    host.setRoot(&button);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {15, 15}, 1}));
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {60, 60}, 1}));
    CHECK_EQ(clicks, 0);

    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {15, 15}, 1}));
    REQUIRE(host.handleEvent(WindowEvent{WindowEvent::Type::FocusLost}));
    CHECK(!button.pressed);
    CHECK_EQ(clicks, 0);
}

VWTEST("button activates with space or enter when focused") {
    PushButton button;
    button.bounds = {10, 10, 40, 20};
    int clicks = 0;
    button.onClick = [&] { ++clicks; };

    WidgetHost host;
    host.setRoot(&button);
    host.setFocus(&button);
    REQUIRE(host.handleEvent(keyDown(Key::Space)));
    REQUIRE(host.handleEvent(keyDown(Key::Enter)));
    CHECK_EQ(clicks, 2);
    CHECK(!host.handleEvent(keyDown(Key::Left)));
}

VWTEST("disabled button ignores clicks and focus") {
    PushButton button;
    button.bounds = {10, 10, 40, 20};
    button.enabled = false;
    int clicks = 0;
    button.onClick = [&] { ++clicks; };

    WidgetHost host;
    host.setRoot(&button);
    host.setFocus(&button);
    CHECK(host.focused() == nullptr);
    CHECK(!host.handleEvent(PointerEvent{PointerAction::Down, {15, 15}, 1}));
    CHECK(!host.handleEvent(PointerEvent{PointerAction::Up, {15, 15}, 1}));
    CHECK_EQ(clicks, 0);
    CHECK(host.cursor() == CursorShape::Arrow);
}

VWTEST("button reports hover and hand cursor") {
    PushButton button;
    button.bounds = {10, 10, 40, 20};

    WidgetHost host;
    host.setRoot(&button);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Move, {15, 15}, 1}));
    CHECK(button.hovered);
    CHECK(host.cursor() == CursorShape::Hand);
    CHECK(!host.handleEvent(PointerEvent{PointerAction::Move, {60, 60}, 1}));
    CHECK(!button.hovered);
}
