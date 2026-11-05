#include "test_framework.h"

#include "ui/editor/MenuPopup.h"
#include "ui/framework/WidgetHost.h"

using namespace vwave::ui;

VWTEST("menu popup activates enabled items") {
    MenuPopup popup;
    popup.bounds = {10, 20, 200, 70};
    int opened = 0;
    int quit = 0;
    popup.setItems({
        {"Open", "Ctrl+O", [&] { ++opened; }, true, false},
        {"", "", {}, true, true},
        {"Quit", "Ctrl+Q", [&] { ++quit; }, true, false},
    });

    WidgetHost host;
    host.setRoot(&popup);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {20, 30}, 1}));
    CHECK(host.cursor() == CursorShape::Hand);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {20, 30}, 1}));
    CHECK_EQ(opened, 1);
    CHECK_EQ(quit, 0);

    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {20, 65}, 1}));
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {20, 65}, 1}));
    CHECK_EQ(quit, 1);
}

VWTEST("menu popup ignores disabled items and separators") {
    MenuPopup popup;
    popup.bounds = {0, 0, 100, 60};
    int actions = 0;
    popup.setItems({
        {"Disabled", "", [&] { ++actions; }, false, false},
        {"", "", {}, true, true},
        {"Enabled", "", [&] { ++actions; }, true, false},
    });
    WidgetHost host;
    host.setRoot(&popup);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {10, 10}, 1}));
    CHECK(host.cursor() == CursorShape::Arrow);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {10, 10}, 1}));
    CHECK_EQ(actions, 0);

    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {10, 45}, 1}));
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {10, 45}, 1}));
    CHECK_EQ(actions, 1);
}
