#include "test_framework.h"

#include "ui/framework/PushButton.h"
#include "ui/framework/Painter.h"
#include "ui/framework/Slider.h"

#include <memory>
#include <string>
#include <vector>

using namespace vwave;
using namespace vwave::ui;

namespace {

struct FillCall {
    Rect rect;
    ColorRole role;
};

struct TextCall {
    std::string text;
    Rect rect;
    ColorRole role;
    FontRole font;
    TextAlign align;
};

struct RecordingPainter : Painter {
    void fillRect(const Rect& rect, ColorRole role) override {
        fills.push_back({rect, role});
    }
    void strokeRect(const Rect& rect, ColorRole role, int thickness) override {
        strokes.push_back({rect, role});
        strokeThickness = thickness;
    }
    void drawLine(Point, Point, ColorRole, int) override {}
    int textWidth(const std::string& text, FontRole) override {
        return static_cast<int>(text.size()) * 7;
    }
    void drawText(const std::string& text, const Rect& rect, ColorRole role,
                  FontRole font, TextAlign align) override {
        texts.push_back({text, rect, role, font, align});
    }
    void pushClip(const Rect& rect) override { clips.push_back(rect); }
    void popClip() override {
        if (!clips.empty()) clips.pop_back();
    }

    std::vector<FillCall> fills;
    std::vector<FillCall> strokes;
    std::vector<TextCall> texts;
    std::vector<Rect> clips;
    int strokeThickness = 1;
};

} // namespace

VWTEST("button paints background border and centered label") {
    PushButton button;
    button.bounds = {10, 20, 80, 24};
    button.label = "Apply";

    RecordingPainter painter;
    button.draw(painter);
    REQUIRE(!painter.fills.empty());
    CHECK(painter.fills[0].role == ColorRole::PanelRaised);
    CHECK_EQ(painter.fills[0].rect.x, 10);
    CHECK_EQ(painter.fills[0].rect.w, 80);
    REQUIRE(!painter.strokes.empty());
    CHECK(painter.strokes[0].role == ColorRole::Border);
    REQUIRE(painter.texts.size() == size_t{1});
    CHECK(painter.texts[0].text == "Apply");
    CHECK(painter.texts[0].role == ColorRole::Text);
    CHECK(painter.texts[0].font == FontRole::Small);
    CHECK(painter.texts[0].align == TextAlign::Center);
}

VWTEST("button paint reflects pressed disabled and accent states") {
    PushButton button;
    button.bounds = {0, 0, 50, 20};
    button.label = "Go";
    button.accent = true;
    RecordingPainter accent;
    button.draw(accent);
    REQUIRE(!accent.fills.empty());
    CHECK(accent.fills[0].role == ColorRole::AccentDark);
    CHECK(accent.texts[0].role == ColorRole::AccentText);

    button.accent = false;
    button.pressed = true;
    RecordingPainter pressed;
    button.draw(pressed);
    CHECK(pressed.fills[0].role == ColorRole::AccentDark);

    button.pressed = false;
    button.enabled = false;
    RecordingPainter disabled;
    button.draw(disabled);
    CHECK(disabled.fills[0].role == ColorRole::Disabled);
    CHECK(disabled.strokes[0].role == ColorRole::TextFaint);
    CHECK(disabled.texts[0].role == ColorRole::TextFaint);
}

VWTEST("slider paints track fill border and handle") {
    ParameterSlider model{0.0, 100.0, 50.0, false, 0.0};
    Slider slider;
    slider.slider = &model;
    slider.bounds = {10, 20, 100, 20};

    RecordingPainter painter;
    slider.draw(painter);
    REQUIRE(painter.fills.size() >= 2);
    CHECK(painter.fills[0].role == ColorRole::Content);
    CHECK_EQ(painter.fills[0].rect.x, 10);
    CHECK_EQ(painter.fills[0].rect.w, 100);
    CHECK(painter.fills[1].role == ColorRole::Accent);
    CHECK_EQ(painter.fills[1].rect.w, 50);
    REQUIRE(!painter.strokes.empty());
    CHECK(painter.strokes[0].role == ColorRole::Border);
    CHECK(painter.fills.back().role == ColorRole::Text);
    CHECK_EQ(painter.fills.back().rect.w, 7);
}

VWTEST("widget tree draws parent before children") {
    class OrderWidget : public Widget {
    public:
        explicit OrderWidget(int value) : value(value) {}
        void paint(Painter& painter) override {
            painter.fillRect(bounds, value == 1 ? ColorRole::Panel
                                                : ColorRole::Accent);
        }
        int value = 0;
    };

    OrderWidget root(1);
    root.bounds = {0, 0, 100, 100};
    root.addChild(std::make_unique<OrderWidget>(2));

    RecordingPainter painter;
    root.draw(painter);
    REQUIRE(painter.fills.size() == size_t{2});
    CHECK(painter.fills[0].role == ColorRole::Panel);
    CHECK(painter.fills[1].role == ColorRole::Accent);
}
