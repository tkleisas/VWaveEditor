#include "ui/sdl/SdlCursorController.h"

namespace vwave::ui {

SDL_SystemCursor systemCursorFor(CursorShape shape) {
    switch (shape) {
    case CursorShape::Hand:
    case CursorShape::OpenHand:
    case CursorShape::ClosedHand:
        return SDL_SYSTEM_CURSOR_HAND;
    case CursorShape::SizeWE:
        return SDL_SYSTEM_CURSOR_SIZEWE;
    case CursorShape::SizeNS:
        return SDL_SYSTEM_CURSOR_SIZENS;
    case CursorShape::SizeAll:
        return SDL_SYSTEM_CURSOR_SIZEALL;
    case CursorShape::Arrow:
    default:
        return SDL_SYSTEM_CURSOR_ARROW;
    }
}

size_t SdlCursorController::indexFor(CursorShape shape) {
    return static_cast<size_t>(shape);
}

SdlCursorController::~SdlCursorController() {
    for (SDL_Cursor* cursor : m_cursors)
        if (cursor) SDL_FreeCursor(cursor);
}

SDL_Cursor* SdlCursorController::cursorFor(CursorShape shape) {
    const size_t index = indexFor(shape);
    if (index >= m_cursors.size()) return nullptr;
    if (!m_cursors[index])
        m_cursors[index] = SDL_CreateSystemCursor(systemCursorFor(shape));
    return m_cursors[index];
}

void SdlCursorController::apply(CursorShape shape) {
    if (shape == m_current) return;
    CursorShape applied = shape;
    SDL_Cursor* cursor = cursorFor(applied);
    if (!cursor && shape != CursorShape::Arrow) {
        applied = CursorShape::Arrow;
        cursor = cursorFor(applied);
    }
    if (!cursor) return;
    SDL_SetCursor(cursor);
    m_current = applied;
}

} // namespace vwave::ui
