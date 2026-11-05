#pragma once

#include "ui/framework/Geometry.h"
#include "ui/framework/Widget.h"

namespace vwave::ui {

int layoutColumn(Widget& parent, Rect area, int spacing = 0,
                 int inset = 0);
int layoutRow(Widget& parent, Rect area, int spacing = 0, int inset = 0);
void layoutStack(Widget& parent, Rect area, int inset = 0);

} // namespace vwave::ui
