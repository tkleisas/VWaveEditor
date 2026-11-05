#include "test_framework.h"

#include "ui/framework/WidgetHost.h"

#include <memory>

using namespace vwave::ui;

namespace {

struct TestWidget : Widget {
    explicit TestWidget(Rect rect, CursorShape shape = CursorShape::Arrow)
        : cursorShape(shape) {
        bounds = rect;
    }

    CursorShape cursorAt(Point) const override { return cursorShape; }

    bool onPointerDown(const PointerEvent&) override {
        ++downCount;
        return acceptDown;
    }
    bool onPointerMove(const PointerEvent&) override {
        ++moveCount;
        return true;
    }
    bool onPointerUp(const PointerEvent&) override {
        ++upCount;
        return true;
    }
    void onHoverChanged(bool hoveredState) override {
        ++hoverChanges;
        hovered = hoveredState;
    }
    void onPointerCaptureLost() override { ++captureLostCount; }

    CursorShape cursorShape = CursorShape::Arrow;
    bool acceptDown = true;
    int downCount = 0;
    int moveCount = 0;
    int upCount = 0;
    int hoverChanges = 0;
    int captureLostCount = 0;
    bool hovered = false;
};

Event pointerEvent(PointerAction action, int x, int y, int button = 1) {
    return PointerEvent{action, {x, y}, button};
}

} // namespace

VWTEST("widget host routes a press to the topmost child") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto first = std::make_unique<TestWidget>(Rect{10, 10, 30, 30});
    auto second = std::make_unique<TestWidget>(Rect{10, 10, 30, 30});
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    root.addChild(std::move(first));
    root.addChild(std::move(second));

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Down, 20, 20)));

    CHECK_EQ(secondPtr->downCount, 1);
    CHECK_EQ(firstPtr->downCount, 0);
    CHECK(host.captured() == secondPtr);
}

VWTEST("captured widget receives movement and release") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto captured = std::make_unique<TestWidget>(Rect{10, 10, 20, 20});
    auto outside = std::make_unique<TestWidget>(Rect{50, 50, 20, 20});
    auto* capturedPtr = captured.get();
    auto* outsidePtr = outside.get();
    root.addChild(std::move(captured));
    root.addChild(std::move(outside));

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Down, 15, 15)));
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Move, 60, 60)));
    CHECK_EQ(capturedPtr->moveCount, 1);
    CHECK_EQ(outsidePtr->moveCount, 0);
    CHECK(host.captured() == capturedPtr);

    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Up, 60, 60)));
    CHECK_EQ(capturedPtr->upCount, 1);
    CHECK(host.captured() == nullptr);
    CHECK(host.hovered() == outsidePtr);
}

VWTEST("unaccepted press does not capture the pointer") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto widget = std::make_unique<TestWidget>(Rect{10, 10, 20, 20});
    auto* widgetPtr = widget.get();
    widget->acceptDown = false;
    root.addChild(std::move(widget));

    WidgetHost host;
    host.setRoot(&root);
    CHECK(host.handleEvent(pointerEvent(PointerAction::Down, 15, 15)));
    CHECK_EQ(widgetPtr->downCount, 1);
    CHECK(host.captured() == nullptr);
}

VWTEST("hover updates cursor and notifies enter and leave") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto widget = std::make_unique<TestWidget>(
        Rect{10, 10, 20, 20}, CursorShape::SizeWE);
    auto* widgetPtr = widget.get();
    root.addChild(std::move(widget));

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Move, 15, 15)));
    CHECK(host.hovered() == widgetPtr);
    CHECK(host.cursor() == CursorShape::SizeWE);
    CHECK(widgetPtr->hovered);
    CHECK_EQ(widgetPtr->hoverChanges, 1);

    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Move, 50, 50)));
    CHECK(host.hovered() == &root);
    CHECK(host.cursor() == CursorShape::Arrow);
    CHECK(!widgetPtr->hovered);
    CHECK_EQ(widgetPtr->hoverChanges, 2);
}

VWTEST("leaving the window and focus loss clear hover and capture") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto widget = std::make_unique<TestWidget>(
        Rect{10, 10, 20, 20}, CursorShape::SizeAll);
    auto* widgetPtr = widget.get();
    root.addChild(std::move(widget));

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Move, 15, 15)));
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Down, 15, 15)));
    CHECK(host.captured() == widgetPtr);

    REQUIRE(host.handleEvent(WindowEvent{WindowEvent::Type::FocusLost}));
    CHECK(host.captured() == nullptr);
    CHECK(host.hovered() == nullptr);
    CHECK(host.cursor() == CursorShape::Arrow);
    CHECK_EQ(widgetPtr->captureLostCount, 1);

    widgetPtr->moveCount = 0;
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Move, 60, 60)));
    CHECK_EQ(widgetPtr->moveCount, 0);
    REQUIRE(host.handleEvent(WindowEvent{WindowEvent::Type::Leave}));
    CHECK(host.hovered() == nullptr);
    CHECK_EQ(widgetPtr->hoverChanges, 2);
}

VWTEST("disabled widgets are skipped by hit testing") {
    Widget root;
    root.bounds = {0, 0, 100, 100};
    auto disabled = std::make_unique<TestWidget>(Rect{10, 10, 20, 20});
    disabled->enabled = false;
    auto* disabledPtr = disabled.get();
    root.addChild(std::move(disabled));

    WidgetHost host;
    host.setRoot(&root);
    REQUIRE(host.handleEvent(pointerEvent(PointerAction::Down, 15, 15)));
    CHECK(host.captured() == nullptr);
    CHECK(host.hovered() == &root);
    CHECK_EQ(disabledPtr->downCount, 0);
}
