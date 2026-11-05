#pragma once

#include "ui/framework/Event.h"

#include <SDL.h>

#include <optional>

namespace vwave::ui {

class SdlEventAdapter {
public:
    std::optional<Event> translate(const SDL_Event& event) const;
};

} // namespace vwave::ui
