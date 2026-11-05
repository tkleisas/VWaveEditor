#include "test_framework.h"

#include "ui/framework/WidgetHost.h"

#include <memory>
#include <string>

using namespace vwave::ui;

namespace {

struct FocusWidget : Widget {
    explicit FocusWidget(Rect rect) { bounds = rect; }

    bool focusable() const override { return canFocus; }
    bool onKeyDown(const KeyEvent& event) override {
        ++keyDownCount;
        lastKey = event.key;
        return consumeKeys;
    }
    bool onKeyUp(const KeyEvent& event) override {
        ++keyUpCount;
        lastKey = event.key;
        return true;
    }
    bool onTextInput(const TextInputEvent& event) override {
        text += event.text;
        return true;
    }
    void onFocusChanged(bool state) override {
        ++focusChanges;
        focused = state;
    }

    bool canFocus = true;
    bool consumeKeys = true;
    int keyDownCount = 0;
    int keyUpCount = 0;
    int focusChanges = 0;
    Key key = Key::Unknown;
    Key lastKey = Key::Unknown;
    std::string text;
    bool focused = false;
};

KeyEvent keyEvent(Key key, bool down = true, bool shift = false) {
    KeyEvent event;
    event.key = key;
    event.down = down;
    event.shift = shift;
    return event;
}

} // namespace

VWTEST("keyboard events go to the focused widget") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<FocusWidget>(Rect{0, 0, 20, 20});
    auto second = std::make_unique<FocusWidget>(Rect{20, 0, 20, 20});
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));

    WidgetHost host;
    host.setRoot(&root);
    host.setFocus(secondPtr);
    CHECK(host.focused() == secondPtr);
    CHECK(!firstPtr->focused);
    CHECK(secondPtr->focused);

    REQUIRE(host.handleEvent(keyEvent(Key::Space)));
    CHECK_EQ(secondPtr->keyDownCount, 1);
    CHECK_EQ(firstPtr->keyDownCount, 0);
    CHECK(secondPtr->lastKey == Key::Space);

    REQUIRE(host.handleEvent(keyEvent(Key::Space, false)));
    CHECK_EQ(secondPtr->keyUpCount, 1);
}

VWTEST("clicking a focusable widget focuses it") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<FocusWidget>(Rect{0, 0, 20, 20});
    auto second = std::make_unique<FocusWidget>(Rect{20, 0, 20, 20});
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));

    WidgetHost host;
    host.setRoot(&root);
    host.setFocus(firstPtr);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {25, 5}, 1}));
    CHECK(host.focused() == secondPtr);
    CHECK(!firstPtr->focused);
    CHECK(secondPtr->focused);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {25, 5}, 1}));
}

VWTEST("tab and shift-tab cycle focusable widgets") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<FocusWidget>(Rect{0, 0, 20, 20});
    auto second = std::make_unique<FocusWidget>(Rect{20, 0, 20, 20});
    auto third = std::make_unique<FocusWidget>(Rect{40, 0, 20, 20});
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    auto* thirdPtr = third.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));
    root.addChild(std::move(third));

    WidgetHost host;
    host.setRoot(&root);
    host.setFocus(firstPtr);

    REQUIRE(host.handleEvent(keyEvent(Key::Tab)));
    CHECK(host.focused() == secondPtr);
    REQUIRE(host.handleEvent(keyEvent(Key::Tab)));
    CHECK(host.focused() == thirdPtr);
    REQUIRE(host.handleEvent(keyEvent(Key::Tab)));
    CHECK(host.focused() == firstPtr);

    REQUIRE(host.handleEvent(keyEvent(Key::Tab, true, true)));
    CHECK(host.focused() == thirdPtr);
    REQUIRE(host.handleEvent(keyEvent(Key::Tab, true, true)));
    CHECK(host.focused() == secondPtr);
}

VWTEST("text input is routed only to the focused widget") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<FocusWidget>(Rect{0, 0, 20, 20});
    auto second = std::make_unique<FocusWidget>(Rect{20, 0, 20, 20});
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));

    WidgetHost host;
    host.setRoot(&root);
    host.setFocus(secondPtr);
    REQUIRE(host.handleEvent(TextInputEvent{"abc"}));
    CHECK(secondPtr->text == "abc");
    CHECK(firstPtr->text.empty());
}

VWTEST("focus change notifies old and new widgets exactly once") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<FocusWidget>(Rect{0, 0, 20, 20});
    auto second = std::make_unique<FocusWidget>(Rect{20, 0, 20, 20});
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));

    WidgetHost host;
    host.setRoot(&root);
    host.setFocus(firstPtr);
    host.setFocus(firstPtr);
    CHECK_EQ(firstPtr->focusChanges, 1);
    host.setFocus(secondPtr);
    CHECK_EQ(firstPtr->focusChanges, 2);
    CHECK_EQ(secondPtr->focusChanges, 1);
    CHECK(!firstPtr->focused);
    CHECK(secondPtr->focused);
}

namespace {

struct WheelWidget : Widget {
    bool onWheel(const WheelEvent& event) override {
        ++count;
        lastDelta = event.deltaY;
        return consume;
    }

    bool consume = true;
    int count = 0;
    double lastDelta = 0.0;
};

} // namespace

VWTEST("wheel events route to the widget under the pointer") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<WheelWidget>();
    first->bounds = {0, 0, 20, 20};
    auto second = std::make_unique<WheelWidget>();
    second->bounds = {20, 0, 20, 20};
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(WheelEvent{{25, 5}, 0.0, -3.0}));
    CHECK_EQ(secondPtr->count, 1);
    CHECK_EQ(firstPtr->count, 0);
    CHECK_NEAR(secondPtr->lastDelta, -3.0, 1e-9);

    secondPtr->consume = false;
    CHECK(!host.handleEvent(WheelEvent{{25, 5}, 0.0, 1.0}));
    CHECK_EQ(secondPtr->count, 2);
}


namespace {

struct BubbleWidget : Widget {
    bool onWheel(const WheelEvent&) override {
        ++count;
        return consume;
    }
    bool consume = false;
    int count = 0;
};

} // namespace

VWTEST("unhandled wheel bubbles to parents") {
    BubbleWidget root;
    root.bounds = {0, 0, 100, 100};
    auto child = std::make_unique<BubbleWidget>();
    child->bounds = {0, 0, 20, 20};
    auto* childPtr = child.get();
    root.addChild(std::move(child));
    root.consume = true;

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(WheelEvent{{5, 5}, 0.0, -1.0}));
    CHECK_EQ(childPtr->count, 1);
    CHECK_EQ(root.count, 1);
}
