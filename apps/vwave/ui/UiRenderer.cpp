#include "ui/UiRenderer.h"

#include "ui/UiTheme.h"

#include <utility>

namespace vwave::ui {

int UiRenderer::textWidth(const std::string& text, TTF_Font* font) const {
    if (!font || text.empty()) return 0;
    int width = 0;
    TTF_SizeUTF8(font, text.c_str(), &width, nullptr);
    return width;
}

int UiRenderer::drawText(const std::string& text, int x, int y,
                         const SDL_Color& color, TTF_Font* font) {
    if (!font || text.empty()) return 0;
    SDL_Surface* surface =
        TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return 0;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    const int width = surface->w;
    const int height = surface->h;
    SDL_FreeSurface(surface);
    if (!texture) return 0;
    SDL_Rect target{x, y, width, height};
    SDL_RenderCopy(m_renderer, texture, nullptr, &target);
    SDL_DestroyTexture(texture);
    return width;
}

void UiRenderer::drawTextCentered(const std::string& text,
                                  const SDL_Rect& rect,
                                  const SDL_Color& color, TTF_Font* font) {
    if (!font) return;
    int width = 0;
    int height = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &width, &height) != 0) return;
    drawText(text, rect.x + (rect.w - width) / 2,
             rect.y + (rect.h - height) / 2, color, font);
}

void UiRenderer::drawButton(const Button& button) {
    SDL_Color fill = button.accent ? kAccentDark : kPanelRaised;
    if (button.active) fill = kAccentDark;
    if (!button.enabled) fill = kDisabled;
    SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(m_renderer, &button.rect);
    SDL_SetRenderDrawColor(m_renderer,
                           button.enabled ? kBorder.r : kTextFaint.r,
                           button.enabled ? kBorder.g : kTextFaint.g,
                           button.enabled ? kBorder.b : kTextFaint.b, 255);
    SDL_RenderDrawRect(m_renderer, &button.rect);
    const SDL_Color textColor = !button.enabled
                                    ? kTextFaint
                                    : (button.accent ? kAccentText : kText);
    drawTextCentered(button.label, button.rect, textColor, m_fontSmall);
}

void UiRenderer::drawSection(const std::string& title, int sidebarWidth,
                             int& y) {
    drawText(title, 18, y, kAccent, m_fontSmall);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 160);
    SDL_RenderDrawLine(m_renderer, 18, y + 17, sidebarWidth - 18, y + 17);
    y += 25;
}

} // namespace vwave::ui
