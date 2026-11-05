#include "ui/editor/ControlPanel.h"

#include <utility>

namespace vwave::ui {

ControlPanel::ControlPanel() {
    m_root.enabled = false;
    m_host.setRoot(&m_root);
}

ControlPanel::~ControlPanel() = default;

void ControlPanel::begin(Rect bounds) {
    m_root.bounds = bounds;
    m_root.visible = true;
    m_buttonIndex = 0;
    m_sliderIndex = 0;
}

PushButton& ControlPanel::addButton(const std::string& label, Rect bounds,
                                    std::function<void()> action, bool accent,
                                    bool active, bool enabled) {
    if (m_buttonIndex >= m_buttons.size()) {
        auto button = std::make_unique<PushButton>();
        auto* pointer = button.get();
        m_root.addChild(std::move(button));
        m_buttons.push_back(pointer);
    }
    auto& button = *m_buttons[m_buttonIndex++];
    button.bounds = bounds;
    button.label = label;
    button.onClick = std::move(action);
    button.accent = accent;
    button.active = active;
    button.enabled = enabled;
    button.visible = true;
    return button;
}

SidebarSlider& ControlPanel::addSlider(
    const std::string& label, const std::string& valueText, Rect bounds,
    vwave::ParameterSlider& slider, std::function<void(double)> onChanged) {
    if (m_sliderIndex >= m_sliders.size()) {
        auto control = std::make_unique<SidebarSlider>();
        auto* pointer = control.get();
        m_root.addChild(std::move(control));
        m_sliders.push_back(pointer);
    }
    auto& control = *m_sliders[m_sliderIndex++];
    control.bounds = bounds;
    control.label = label;
    control.valueText = valueText;
    control.slider = &slider;
    control.onChanged = std::move(onChanged);
    control.visible = true;
    return control;
}

void ControlPanel::end(Painter* painter) {
    for (size_t i = m_buttonIndex; i < m_buttons.size(); ++i)
        m_buttons[i]->visible = false;
    for (size_t i = m_sliderIndex; i < m_sliders.size(); ++i)
        m_sliders[i]->visible = false;
    if (painter) m_root.draw(*painter);
}

} // namespace vwave::ui
