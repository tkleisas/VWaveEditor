#pragma once

#include "ui/framework/Geometry.h"

#include <string>

namespace vwave::ui {

enum class ColorRole {
    Background,
    Panel,
    PanelRaised,
    Content,
    Border,
    Text,
    TextDim,
    TextFaint,
    Accent,
    AccentDark,
    AccentText,
    Disabled,
};

enum class FontRole {
    Small,
    Normal,
    Bold,
    Title,
};

enum class TextAlign {
    Start,
    Center,
    End,
};

class Painter {
public:
    virtual ~Painter() = default;

    virtual void fillRect(const Rect& rect, ColorRole role) = 0;
    virtual void strokeRect(const Rect& rect, ColorRole role,
                            int thickness = 1) = 0;
    virtual void drawLine(Point from, Point to, ColorRole role,
                          int thickness = 1) = 0;
    virtual int textWidth(const std::string& text, FontRole font) = 0;
    virtual void drawText(const std::string& text, const Rect& rect,
                          ColorRole role, FontRole font,
                          TextAlign align = TextAlign::Start) = 0;
    virtual void pushClip(const Rect& rect) = 0;
    virtual void popClip() = 0;
};

} // namespace vwave::ui
