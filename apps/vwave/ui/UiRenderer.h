#pragma once

#include "ui/Widgets.h"

#include <SDL_ttf.h>

#include <functional>
#include <string>
#include <vector>

namespace vwave::ui {

class UiRenderer {
public:
    void setRenderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void setFonts(TTF_Font* small, TTF_Font* normal, TTF_Font* bold,
                  TTF_Font* title) {
        m_fontSmall = small;
        m_fontNormal = normal;
        m_fontBold = bold;
        m_fontTitle = title;
    }

    SDL_Renderer* renderer() const { return m_renderer; }
    TTF_Font* fontSmall() const { return m_fontSmall; }
    TTF_Font* fontNormal() const { return m_fontNormal; }
    TTF_Font* fontBold() const { return m_fontBold; }
    TTF_Font* fontTitle() const { return m_fontTitle; }

    void beginFrame();
    void setControlArea(int area) { m_controlArea = area; }
    int controlArea() const { return m_controlArea; }

    const std::vector<Button>& buttons() const { return m_buttons; }
    const std::vector<SliderRef>& sliders() const { return m_sliders; }

    int textWidth(const std::string& text, TTF_Font* font) const;
    int drawText(const std::string& text, int x, int y, const SDL_Color& color,
                 TTF_Font* font);
    void drawTextCentered(const std::string& text, const SDL_Rect& rect,
                          const SDL_Color& color, TTF_Font* font);
    void drawButton(const Button& button);
    void addButton(const std::string& label, int x, int y, int w, int h,
                   std::function<void()> action, bool accent, bool active,
                   bool enabled);
    void addSlider(const std::string& label, const std::string& valueText,
                   int x, int y, int w, vwave::ParameterSlider& slider,
                   std::function<void(double)> onChanged);
    void setSliderFromX(SliderRef& slider, int x);
    void drawSection(const std::string& title, int sidebarWidth, int& y);

private:
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_fontSmall = nullptr;
    TTF_Font* m_fontNormal = nullptr;
    TTF_Font* m_fontBold = nullptr;
    TTF_Font* m_fontTitle = nullptr;
    int m_controlArea = 1;
    std::vector<Button> m_buttons;
    std::vector<SliderRef> m_sliders;
};

} // namespace vwave::ui
