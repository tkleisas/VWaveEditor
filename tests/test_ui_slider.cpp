#include "test_framework.h"

#include "ui/framework/Slider.h"
#include "ui/framework/WidgetHost.h"

using namespace vwave;
using namespace vwave::ui;

namespace {

void configureSlider(Slider& slider, ParameterSlider& model) {
    slider.slider = &model;
    slider.bounds = {0, 0, 100, 10};
}

KeyEvent keyDown(Key key) {
    KeyEvent event;
    event.key = key;
    event.down = true;
    return event;
}

} // namespace

VWTEST("slider maps clicks to normalized positions") {
    ParameterSlider model{0.0, 100.0, 0.0, false, 0.0};
    Slider slider;
    configureSlider(slider, model);
    int changes = 0;
    slider.onChanged = [&](double) { ++changes; };

    WidgetHost host;
    host.setRoot(&slider);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {0, 5}, 1}));
    CHECK_NEAR(model.value, 0.0, 1e-9);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Move, {50, 5}, 1}));
    CHECK_NEAR(model.value, 50.0, 1e-9);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Move, {100, 5}, 1}));
    CHECK_NEAR(model.value, 100.0, 1e-9);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {100, 5}, 1}));
    CHECK_EQ(changes, 3);
}

VWTEST("slider release ends drag and movement no longer updates") {
    ParameterSlider model{0.0, 100.0, 0.0, false, 0.0};
    Slider slider;
    configureSlider(slider, model);

    WidgetHost host;
    host.setRoot(&slider);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {20, 5}, 1}));
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Up, {20, 5}, 1}));
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Move, {80, 5}, 1}));
    CHECK_NEAR(model.value, 20.0, 1e-9);
}

VWTEST("slider capture loss cancels dragging") {
    ParameterSlider model{0.0, 100.0, 0.0, false, 0.0};
    Slider slider;
    configureSlider(slider, model);

    WidgetHost host;
    host.setRoot(&slider);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {20, 5}, 1}));
    REQUIRE(host.handleEvent(WindowEvent{WindowEvent::Type::FocusLost}));
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Move, {80, 5}, 1}));
    CHECK_NEAR(model.value, 20.0, 1e-9);
}

VWTEST("slider keyboard steps and jumps") {
    ParameterSlider model{0.0, 100.0, 50.0, false, 10.0};
    Slider slider;
    configureSlider(slider, model);

    WidgetHost host;
    host.setRoot(&slider);
    host.setFocus(&slider);
    REQUIRE(host.handleEvent(keyDown(Key::Right)));
    CHECK_NEAR(model.value, 60.0, 1e-9);
    REQUIRE(host.handleEvent(keyDown(Key::Left)));
    CHECK_NEAR(model.value, 50.0, 1e-9);
    REQUIRE(host.handleEvent(keyDown(Key::Up)));
    CHECK_NEAR(model.value, 60.0, 1e-9);
    REQUIRE(host.handleEvent(keyDown(Key::Down)));
    CHECK_NEAR(model.value, 50.0, 1e-9);
    REQUIRE(host.handleEvent(keyDown(Key::Home)));
    CHECK_NEAR(model.value, 0.0, 1e-9);
    REQUIRE(host.handleEvent(keyDown(Key::End)));
    CHECK_NEAR(model.value, 100.0, 1e-9);
}

VWTEST("slider respects logarithmic and stepped models") {
    ParameterSlider model{20.0, 20000.0, 20.0, true, 0.0};
    Slider slider;
    configureSlider(slider, model);

    WidgetHost host;
    host.setRoot(&slider);
    REQUIRE(host.handleEvent(PointerEvent{PointerAction::Down, {50, 5}, 1}));
    CHECK_NEAR(model.value, 632.455532, 1e-5);

    ParameterSlider stepped{0.0, 10.0, 0.0, false, 1.0};
    Slider snapped;
    configureSlider(snapped, stepped);
    WidgetHost secondHost;
    secondHost.setRoot(&snapped);
    REQUIRE(secondHost.handleEvent(
        PointerEvent{PointerAction::Down, {57, 5}, 1}));
    CHECK_NEAR(stepped.value, 6.0, 1e-9);
}

VWTEST("disabled slider cannot be focused or dragged") {
    ParameterSlider model{0.0, 100.0, 0.0, false, 0.0};
    Slider slider;
    configureSlider(slider, model);
    slider.enabled = false;

    WidgetHost host;
    host.setRoot(&slider);
    host.setFocus(&slider);
    CHECK(host.focused() == nullptr);
    CHECK(!host.handleEvent(PointerEvent{PointerAction::Down, {50, 5}, 1}));
    CHECK_NEAR(model.value, 0.0, 1e-9);
    CHECK(host.cursor() == CursorShape::Arrow);
}
