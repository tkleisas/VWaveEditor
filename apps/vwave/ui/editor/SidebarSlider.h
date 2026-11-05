#pragma once

#include "ui/framework/Slider.h"

#include <string>

namespace vwave::ui {

class SidebarSlider : public Slider {
public:
    std::string label;
    std::string valueText;

protected:
    void paint(Painter& painter) override;
};

} // namespace vwave::ui
