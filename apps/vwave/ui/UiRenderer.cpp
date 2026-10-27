#include "ui/UiRenderer.h"

#include "ui/UiTheme.h"

#include <utility>

namespace vwave::ui {

void UiRenderer::beginFrame() {
    m_buttons.clear();
    m_sliders.clear();
}

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

void UiRenderer::addButton(const std::string& label, int x, int y, int w,
                           int h, std::function<void()> action, bool accent,
                           bool active, bool enabled) {
    Button button;
    button.rect = {x, y, w, h};
    button.label = label;
    button.action = std::move(action);
    button.accent = accent;
    button.active = active;
    button.enabled = enabled;
    button.area = m_controlArea;
    m_buttons.push_back(button);
    drawButton(button);
}

void UiRenderer::addSlider(const std::string& label,
                           const std::string& valueText, int x, int y, int w,
                           vwave::ParameterSlider& slider,
                           std::function<void(double)> onChanged) {
    drawText(label, x, y, kTextDim, m_fontSmall);
    const int valueWidth = textWidth(valueText, m_fontSmall);
    drawText(valueText, x + w - valueWidth, y, kText, m_fontSmall);
    SDL_Rect track{x, y + 17, w, 6};
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &track);
    const double normalized = slider.normalized();
    SDL_Rect filled{track.x, track.y, static_cast<int>(normalized * track.w),
                    track.h};
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 210);
    SDL_RenderFillRect(m_renderer, &filled);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &track);
    const int handleX = track.x + static_cast<int>(normalized * track.w);
    SDL_Rect handle{handleX - 3, track.y - 4, 7, track.h + 8};
    SDL_SetRenderDrawColor(m_renderer, kText.r, kText.g, kText.b, 255);
    SDL_RenderFillRect(m_renderer, &handle);
    m_sliders.push_back({&slider, track, label, m_controlArea,
                         std::move(onChanged)});
}

void UiRenderer::setSliderFromX(SliderRef& slider, int x) {
    if (!slider.slider || slider.track.w <= 0) return;
    const double position =
        static_cast<double>(x - slider.track.x) / slider.track.w;
    slider.slider->setNormalized(position);
    if (slider.onChanged) slider.onChanged(slider.slider->value);
}

void UiRenderer::drawSection(const std::string& title, int sidebarWidth,
                             int& y) {
    drawText(title, 18, y, kAccent, m_fontSmall);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 160);
    SDL_RenderDrawLine(m_renderer, 18, y + 17, sidebarWidth - 18, y + 17);
    y += 25;
}

} // namespace vwave::ui
