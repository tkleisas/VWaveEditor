#include "test_framework.h"

#include "ui/framework/Layout.h"
#include "ui/framework/Widget.h"

#include <memory>

using namespace vwave::ui;

namespace {

struct SizedWidget : Widget {
    SizedWidget(int width, int height) {
        bounds.w = width;
        bounds.h = height;
    }
};

} // namespace

VWTEST("column layout stacks visible children and reports height") {
    Widget parent;
    auto first = std::make_unique<SizedWidget>(40, 20);
    auto second = std::make_unique<SizedWidget>(50, 30);
    auto hidden = std::make_unique<SizedWidget>(10, 10);
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    auto* hiddenPtr = hidden.get();
    parent.addChild(std::move(first));
    parent.addChild(std::move(second));
    parent.addChild(std::move(hidden));
    hiddenPtr->visible = false;

    const int height =
        layoutColumn(parent, Rect{10, 20, 100, 200}, 4, 5);
    CHECK_EQ(height, 64);
    CHECK_EQ(firstPtr->bounds.x, 15);
    CHECK_EQ(firstPtr->bounds.y, 25);
    CHECK_EQ(firstPtr->bounds.w, 90);
    CHECK_EQ(firstPtr->bounds.h, 20);
    CHECK_EQ(secondPtr->bounds.x, 15);
    CHECK_EQ(secondPtr->bounds.y, 49);
    CHECK_EQ(secondPtr->bounds.w, 90);
    CHECK_EQ(secondPtr->bounds.h, 30);
    CHECK_EQ(hiddenPtr->bounds.y, 0);
}

VWTEST("row layout arranges widths and reports width") {
    Widget parent;
    auto first = std::make_unique<SizedWidget>(20, 10);
    auto second = std::make_unique<SizedWidget>(30, 12);
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    parent.addChild(std::move(first));
    parent.addChild(std::move(second));

    const int width = layoutRow(parent, Rect{10, 20, 200, 100}, 6, 3);
    CHECK_EQ(width, 62);
    CHECK_EQ(firstPtr->bounds.x, 13);
    CHECK_EQ(firstPtr->bounds.y, 23);
    CHECK_EQ(firstPtr->bounds.w, 20);
    CHECK_EQ(firstPtr->bounds.h, 94);
    CHECK_EQ(secondPtr->bounds.x, 39);
    CHECK_EQ(secondPtr->bounds.w, 30);
}

VWTEST("stack layout fills each visible child") {
    Widget parent;
    auto first = std::make_unique<SizedWidget>(1, 1);
    auto second = std::make_unique<SizedWidget>(2, 2);
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    parent.addChild(std::move(first));
    parent.addChild(std::move(second));

    layoutStack(parent, Rect{5, 6, 100, 50}, 7);
    for (Widget* child : {firstPtr, secondPtr}) {
        CHECK_EQ(child->bounds.x, 12);
        CHECK_EQ(child->bounds.y, 13);
        CHECK_EQ(child->bounds.w, 86);
        CHECK_EQ(child->bounds.h, 36);
    }
}

VWTEST("layout functions handle empty and undersized areas") {
    Widget parent;
    CHECK_EQ(layoutColumn(parent, Rect{0, 0, 0, 0}, 4, 5), 10);
    auto child = std::make_unique<SizedWidget>(20, 10);
    auto* childPtr = child.get();
    parent.addChild(std::move(child));
    layoutRow(parent, Rect{0, 0, 5, 5}, 2, 10);
    CHECK_EQ(childPtr->bounds.w, 20);
    CHECK_EQ(childPtr->bounds.h, 0);
}
