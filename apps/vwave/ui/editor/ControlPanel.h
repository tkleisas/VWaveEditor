#pragma once

#include "ui/editor/SidebarSlider.h"
#include "ui/framework/Painter.h"
#include "ui/framework/PushButton.h"
#include "ui/framework/WidgetHost.h"

#include <functional>
#include <memory>
#include <vector>

namespace vwave::ui {

class ControlPanel {
public:
    ControlPanel();
    ~ControlPanel();

    ControlPanel(const ControlPanel&) = delete;
    ControlPanel& operator=(const ControlPanel&) = delete;

    void begin(Rect bounds);
    PushButton& addButton(const std::string& label, Rect bounds,
                          std::function<void()> action, bool accent = false,
                          bool active = false, bool enabled = true);
    SidebarSlider& addSlider(const std::string& label,
                             const std::string& valueText, Rect bounds,
                             vwave::ParameterSlider& slider,
                             std::function<void(double)> onChanged = {});
    void end(Painter* painter);

    bool handleEvent(const Event& event) { return m_host.handleEvent(event); }
    Widget* hovered() const { return m_host.hovered(); }
    Widget* captured() const { return m_host.captured(); }
    CursorShape cursor() const { return m_host.cursor(); }

    const std::vector<PushButton*>& buttons() const { return m_buttons; }
    const std::vector<SidebarSlider*>& sliders() const { return m_sliders; }

private:
    Widget m_root;
    WidgetHost m_host;
    std::vector<PushButton*> m_buttons;
    std::vector<SidebarSlider*> m_sliders;
    size_t m_buttonIndex = 0;
    size_t m_sliderIndex = 0;
};

} // namespace vwave::ui
