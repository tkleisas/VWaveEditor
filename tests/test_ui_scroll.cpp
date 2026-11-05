#include "test_framework.h"

#include "ui/framework/Painter.h"
#include "ui/framework/ScrollView.h"
#include "ui/framework/WidgetHost.h"

#include <memory>
#include <vector>

using namespace vwave::ui;

namespace {

struct PassiveWidget : Widget {
    bool onWheel(const WheelEvent&) override { return false; }
};

struct ClipPainter : Painter {
    void fillRect(const Rect&, ColorRole) override {}
    void strokeRect(const Rect&, ColorRole, int) override {}
    void drawLine(Point, Point, ColorRole, int) override {}
    int textWidth(const std::string&, FontRole) override { return 0; }
    void drawText(const std::string&, const Rect&, ColorRole, FontRole,
                  TextAlign) override {}
    void pushClip(const Rect& rect) override { clips.push_back(rect); }
    void popClip() override {
        if (!clips.empty()) clips.pop_back();
    }
    std::vector<Rect> clips;
};

} // namespace

VWTEST("scroll view clamps and translates content") {
    ScrollView scroll;
    scroll.bounds = {0, 0, 100, 50};
    auto content = std::make_unique<PassiveWidget>();
    content->bounds = {0, 0, 100, 200};
    auto* contentPtr = content.get();
    scroll.setContent(std::move(content));
    scroll.contentHeight = 200;

    scroll.setScrollOffset(40);
    CHECK_EQ(scroll.scrollOffset(), 40);
    CHECK_EQ(contentPtr->bounds.y, -40);

    scroll.setScrollOffset(-10);
    CHECK_EQ(scroll.scrollOffset(), 0);
    CHECK_EQ(contentPtr->bounds.y, 0);

    scroll.setScrollOffset(1000);
    CHECK_EQ(scroll.scrollOffset(), 150);
    CHECK_EQ(contentPtr->bounds.y, -150);
}

VWTEST("scroll view ignores wheels when content fits") {
    ScrollView scroll;
    scroll.bounds = {0, 0, 100, 50};
    auto content = std::make_unique<PassiveWidget>();
    content->bounds = {0, 0, 100, 40};
    scroll.setContent(std::move(content));
    scroll.contentHeight = 40;

    CHECK(!scroll.onWheel(WheelEvent{{10, 10}, 0.0, -3.0}));
    CHECK_EQ(scroll.scrollOffset(), 0);
}

VWTEST("scroll wheel bubbles from child to scroll view") {
    ScrollView scroll;
    scroll.bounds = {0, 0, 100, 50};
    auto content = std::make_unique<PassiveWidget>();
    content->bounds = {0, 0, 100, 200};
    auto child = std::make_unique<PassiveWidget>();
    child->bounds = {0, 0, 100, 100};
    auto* childPtr = child.get();
    content->addChild(std::move(child));
    scroll.setContent(std::move(content));
    scroll.contentHeight = 200;

    WidgetHost host;
    host.setRoot(&scroll);
    REQUIRE(host.handleEvent(WheelEvent{{10, 10}, 0.0, -1.0}));
    CHECK_EQ(scroll.scrollOffset(), 48);
    CHECK_EQ(childPtr->bounds.y, -48);
}

VWTEST("scroll view clips its content while drawing") {
    ScrollView scroll;
    scroll.bounds = {10, 20, 100, 50};
    auto content = std::make_unique<PassiveWidget>();
    content->bounds = {10, 20, 100, 200};
    scroll.setContent(std::move(content));
    scroll.contentHeight = 200;

    ClipPainter painter;
    scroll.draw(painter);
    CHECK(painter.clips.empty());
}
