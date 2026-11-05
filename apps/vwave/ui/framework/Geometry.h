#pragma once

namespace vwave::ui {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(Point point) const {
        return point.x >= x && point.y >= y && point.x < x + w &&
               point.y < y + h;
    }
};

enum class CursorShape {
    Arrow,
    Hand,
    OpenHand,
    ClosedHand,
    SizeWE,
    SizeNS,
    SizeAll,
};

} // namespace vwave::ui
