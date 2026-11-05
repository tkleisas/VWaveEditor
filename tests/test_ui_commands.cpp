#include "test_framework.h"

#include "ui/framework/CommandRegistry.h"

#include <string>

using namespace vwave::ui;

namespace {

KeyEvent keyDown(Key key, bool ctrl = false, bool shift = false,
                 bool alt = false) {
    KeyEvent event;
    event.key = key;
    event.down = true;
    event.ctrl = ctrl;
    event.shift = shift;
    event.alt = alt;
    return event;
}

} // namespace

VWTEST("commands execute by stable id") {
    CommandRegistry commands;
    int count = 0;
    REQUIRE(commands.add({"edit.gain", "Gain", {}, {}, [&] { ++count; }}));
    CHECK(commands.invoke("edit.gain"));
    CHECK_EQ(count, 1);
    CHECK_EQ(commands.size(), size_t{1});
    CHECK(commands.find("edit.gain") != nullptr);
    CHECK(commands.find("missing") == nullptr);
}

VWTEST("disabled commands do not execute") {
    CommandRegistry commands;
    bool enabled = false;
    int count = 0;
    REQUIRE(commands.add(
        {"edit.gain", "Gain", {}, [&] { return enabled; }, [&] { ++count; }}));
    CHECK(!commands.invoke("edit.gain"));
    CHECK_EQ(count, 0);
    CHECK(!commands.find("edit.gain")->enabled());
    enabled = true;
    CHECK(commands.invoke("edit.gain"));
    CHECK_EQ(count, 1);
}

VWTEST("duplicate command ids are rejected") {
    CommandRegistry commands;
    int first = 0;
    int second = 0;
    REQUIRE(commands.add({"edit.gain", "Gain", {}, {}, [&] { ++first; }}));
    CHECK(!commands.add({"edit.gain", "Other", {}, {}, [&] { ++second; }}));
    CHECK(commands.invoke("edit.gain"));
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 0);
}

VWTEST("shortcut matching requires exact modifiers") {
    CommandRegistry commands;
    int count = 0;
    REQUIRE(commands.add(
        {"edit.undo", "Undo", {Key::Space, true, false, false}, {},
         [&] { ++count; }}));

    CHECK(commands.executeShortcut(keyDown(Key::Space, true)));
    CHECK_EQ(count, 1);
    CHECK(!commands.executeShortcut(keyDown(Key::Space, false)));
    CHECK(!commands.executeShortcut(keyDown(Key::Space, false, true)));
    CHECK(!commands.executeShortcut(keyDown(Key::Space, true, true)));
    CHECK(!commands.executeShortcut(keyDown(Key::Space, true, false, true)));
    CHECK_EQ(count, 1);
}

VWTEST("shortcut execution ignores key up and repeat") {
    CommandRegistry commands;
    int count = 0;
    REQUIRE(commands.add(
        {"edit.undo", "Undo", {Key::Space, false, false, false}, {},
         [&] { ++count; }}));
    KeyEvent up = keyDown(Key::Space);
    up.down = false;
    CHECK(!commands.executeShortcut(up));
    KeyEvent repeat = keyDown(Key::Space);
    repeat.repeat = true;
    CHECK(!commands.executeShortcut(repeat));
    CHECK_EQ(count, 0);
}

VWTEST("disabled shortcut is not executed") {
    CommandRegistry commands;
    int count = 0;
    REQUIRE(commands.add(
        {"edit.undo", "Undo", {Key::Space, false, false, false},
         [&] { return false; }, [&] { ++count; }}));
    CHECK(!commands.executeShortcut(keyDown(Key::Space)));
    CHECK_EQ(count, 0);
}
