#include "ui/sdl/SdlPainter.h"

#include "ui/UiTheme.h"

#include <algorithm>
#include <utility>

namespace vwave::ui {

namespace {

std::string cacheKey(const std::string& text, ColorRole role, FontRole font) {
    std::string key;
    key.reserve(text.size() + 2);
    key.push_back(static_cast<char>(role));
    key.push_back(static_cast<char>(font));
    key += text;
    return key;
}

} // namespace

SdlPainter::SdlPainter(SDL_Renderer* renderer, TTF_Font* small,
                       TTF_Font* normal, TTF_Font* bold, TTF_Font* title)
    : m_renderer(renderer),
      m_small(small),
      m_normal(normal),
      m_bold(bold),
      m_title(title) {}

SdlPainter::~SdlPainter() { clearTextCache(); }

SDL_Color SdlPainter::colorFor(ColorRole role) const {
    switch (role) {
    case ColorRole::Background: return kBackground;
    case ColorRole::Panel: return kPanel;
    case ColorRole::PanelRaised: return kPanelRaised;
    case ColorRole::Content: return kContent;
    case ColorRole::Border: return kBorder;
    case ColorRole::Text: return kText;
    case ColorRole::TextDim: return kTextDim;
    case ColorRole::TextFaint: return kTextFaint;
    case ColorRole::Accent: return kAccent;
    case ColorRole::AccentDark: return kAccentDark;
    case ColorRole::AccentText: return kAccentText;
    case ColorRole::Disabled: return kDisabled;
    }
    return kText;
}

TTF_Font* SdlPainter::fontFor(FontRole role) const {
    switch (role) {
    case FontRole::Small: return m_small;
    case FontRole::Normal: return m_normal;
    case FontRole::Bold: return m_bold;
    case FontRole::Title: return m_title;
    }
    return m_normal;
}

void SdlPainter::applyColor(ColorRole role) {
    const SDL_Color color = colorFor(role);
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
}

void SdlPainter::fillRect(const Rect& rect, ColorRole role) {
    if (rect.w <= 0 || rect.h <= 0) return;
    applyColor(role);
    const SDL_Rect target{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderFillRect(m_renderer, &target);
}

void SdlPainter::strokeRect(const Rect& rect, ColorRole role, int thickness) {
    if (rect.w <= 0 || rect.h <= 0 || thickness <= 0) return;
    applyColor(role);
    for (int i = 0; i < thickness; ++i) {
        const SDL_Rect target{rect.x + i, rect.y + i, rect.w - i * 2,
                              rect.h - i * 2};
        if (target.w <= 0 || target.h <= 0) break;
        SDL_RenderDrawRect(m_renderer, &target);
    }
}

void SdlPainter::drawLine(Point from, Point to, ColorRole role,
                          int thickness) {
    if (thickness <= 0) return;
    applyColor(role);
    if (thickness == 1) {
        SDL_RenderDrawLine(m_renderer, from.x, from.y, to.x, to.y);
        return;
    }
    const int offset = thickness / 2;
    for (int i = -offset; i <= offset; ++i)
        SDL_RenderDrawLine(m_renderer, from.x, from.y + i, to.x, to.y + i);
}

int SdlPainter::textWidth(const std::string& text, FontRole font) {
    TTF_Font* selected = fontFor(font);
    if (!selected || text.empty()) return 0;
    int width = 0;
    TTF_SizeUTF8(selected, text.c_str(), &width, nullptr);
    return width;
}

SdlPainter::CachedText& SdlPainter::cachedText(const std::string& text,
                                               ColorRole role,
                                               FontRole font) {
    const std::string key = cacheKey(text, role, font);
    auto it = m_textCache.find(key);
    if (it != m_textCache.end()) return it->second;
    if (m_textCache.size() >= 512) clearTextCache();

    CachedText cached;
    TTF_Font* selected = fontFor(font);
    if (selected && !text.empty()) {
        SDL_Surface* surface =
            TTF_RenderUTF8_Blended(selected, text.c_str(), colorFor(role));
        if (surface) {
            cached.width = surface->w;
            cached.height = surface->h;
            cached.texture =
                SDL_CreateTextureFromSurface(m_renderer, surface);
            SDL_FreeSurface(surface);
        }
    }
    return m_textCache.emplace(key, cached).first->second;
}

void SdlPainter::drawText(const std::string& text, const Rect& rect,
                          ColorRole role, FontRole font, TextAlign align) {
    if (text.empty()) return;
    CachedText& cached = cachedText(text, role, font);
    if (!cached.texture) return;
    int x = rect.x;
    if (align == TextAlign::Center)
        x = rect.x + (rect.w - cached.width) / 2;
    else if (align == TextAlign::End)
        x = rect.x + rect.w - cached.width;
    const int y = rect.y + (rect.h - cached.height) / 2;
    const SDL_Rect target{x, y, cached.width, cached.height};
    SDL_RenderCopy(m_renderer, cached.texture, nullptr, &target);
}

void SdlPainter::pushClip(const Rect& rect) {
    m_clipStack.push_back(rect);
    const SDL_Rect target{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderSetClipRect(m_renderer, &target);
}

void SdlPainter::popClip() {
    if (!m_clipStack.empty()) m_clipStack.pop_back();
    if (m_clipStack.empty()) {
        SDL_RenderSetClipRect(m_renderer, nullptr);
        return;
    }
    const Rect& rect = m_clipStack.back();
    const SDL_Rect target{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderSetClipRect(m_renderer, &target);
}

void SdlPainter::clearTextCache() {
    for (auto& entry : m_textCache)
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    m_textCache.clear();
}

} // namespace vwave::ui
