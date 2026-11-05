#include "test_framework.h"

#include "ui/editor/ControlPanel.h"
#include "ui/framework/WidgetHost.h"

using namespace vwave;
using namespace vwave::ui;

VWTEST("control panel pools and hides controls") {
    ControlPanel panel;
    int clicks = 0;
    panel.begin({0, 0, 200, 100});
    auto& first = panel.addButton("One", {10, 10, 40, 20}, [&] { ++clicks; });
    panel.addButton("Two", {60, 10, 40, 20}, [] {});
    panel.end(nullptr);
    CHECK_EQ(panel.buttons().size(), size_t{2});
    CHECK(first.visible);

    panel.begin({0, 0, 200, 100});
    panel.addButton("Only", {10, 10, 40, 20}, [&] { ++clicks; });
    panel.end(nullptr);
    CHECK(panel.buttons()[0]->visible);
    CHECK(!panel.buttons()[1]->visible);

    REQUIRE(panel.handleEvent(
        PointerEvent{PointerAction::Down, {15, 15}, 1}));
    REQUIRE(panel.handleEvent(
        PointerEvent{PointerAction::Up, {15, 15}, 1}));
    CHECK_EQ(clicks, 1);
}

VWTEST("control panel pools sliders") {
    ControlPanel panel;
    ParameterSlider model{0.0, 100.0, 0.0, false, 0.0};
    panel.begin({0, 0, 200, 100});
    auto& slider =
        panel.addSlider("Gain", "0", {10, 10, 100, 26}, model);
    panel.end(nullptr);
    CHECK_EQ(panel.sliders().size(), size_t{1});
    CHECK(slider.visible);

    panel.begin({0, 0, 200, 100});
    panel.end(nullptr);
    CHECK(!panel.sliders()[0]->visible);
}
