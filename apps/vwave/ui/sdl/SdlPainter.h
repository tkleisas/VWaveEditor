#pragma once

#include "ui/framework/Painter.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace vwave::ui {

class SdlPainter : public Painter {
public:
    SdlPainter(SDL_Renderer* renderer, TTF_Font* small, TTF_Font* normal,
               TTF_Font* bold, TTF_Font* title);
    ~SdlPainter() override;

    SdlPainter(const SdlPainter&) = delete;
    SdlPainter& operator=(const SdlPainter&) = delete;

    void fillRect(const Rect& rect, ColorRole role) override;
    void strokeRect(const Rect& rect, ColorRole role,
                    int thickness = 1) override;
    void drawLine(Point from, Point to, ColorRole role,
                  int thickness = 1) override;
    int textWidth(const std::string& text, FontRole font) override;
    void drawText(const std::string& text, const Rect& rect, ColorRole role,
                  FontRole font, TextAlign align = TextAlign::Start) override;
    void pushClip(const Rect& rect) override;
    void popClip() override;

    void clearTextCache();

private:
    struct CachedText {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };

    SDL_Color colorFor(ColorRole role) const;
    TTF_Font* fontFor(FontRole role) const;
    CachedText& cachedText(const std::string& text, ColorRole role,
                           FontRole font);
    void applyColor(ColorRole role);

    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_small = nullptr;
    TTF_Font* m_normal = nullptr;
    TTF_Font* m_bold = nullptr;
    TTF_Font* m_title = nullptr;
    std::unordered_map<std::string, CachedText> m_textCache;
    std::vector<Rect> m_clipStack;
};

} // namespace vwave::ui
