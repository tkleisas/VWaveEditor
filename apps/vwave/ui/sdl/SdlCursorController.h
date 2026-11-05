#pragma once

#include "ui/framework/Geometry.h"

#include <SDL.h>

#include <array>

namespace vwave::ui {

SDL_SystemCursor systemCursorFor(CursorShape shape);

class SdlCursorController {
public:
    SdlCursorController() = default;
    ~SdlCursorController();

    SdlCursorController(const SdlCursorController&) = delete;
    SdlCursorController& operator=(const SdlCursorController&) = delete;

    void apply(CursorShape shape);
    CursorShape current() const { return m_current; }

private:
    static constexpr size_t kCursorCount = 7;
    static size_t indexFor(CursorShape shape);
    SDL_Cursor* cursorFor(CursorShape shape);

    std::array<SDL_Cursor*, kCursorCount> m_cursors{};
    CursorShape m_current = CursorShape::Arrow;
};

} // namespace vwave::ui
