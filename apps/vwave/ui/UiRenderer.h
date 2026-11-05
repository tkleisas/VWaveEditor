#pragma once

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

    int textWidth(const std::string& text, TTF_Font* font) const;
    int drawText(const std::string& text, int x, int y, const SDL_Color& color,
                 TTF_Font* font);
    void drawTextCentered(const std::string& text, const SDL_Rect& rect,
                          const SDL_Color& color, TTF_Font* font);
    void drawSection(const std::string& title, int sidebarWidth, int& y);

private:
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_fontSmall = nullptr;
    TTF_Font* m_fontNormal = nullptr;
    TTF_Font* m_fontBold = nullptr;
    TTF_Font* m_fontTitle = nullptr;
};

} // namespace vwave::ui
