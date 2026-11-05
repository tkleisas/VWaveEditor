#pragma once

#include "ui/framework/Widget.h"

#include <memory>

namespace vwave::ui {

class ScrollView : public Widget {
public:
    Widget& setContent(std::unique_ptr<Widget> content);
    Widget* content() const { return m_content; }

    void setScrollOffset(int offset);
    int scrollOffset() const { return m_offset; }
    int maxScroll() const;

    bool onWheel(const WheelEvent& event) override;
    void draw(Painter& painter) override;

    int contentHeight = 0;
    int wheelStep = 48;

private:
    Widget* m_content = nullptr;
    int m_offset = 0;
};

} // namespace vwave::ui
