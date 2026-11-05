#pragma once

#include "vwave/UiModel.h"

#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace vwave::ui {

struct Button {
    SDL_Rect rect{};
    std::string label;
    std::function<void()> action;
    bool accent = false;
    bool active = false;
    bool enabled = true;
    int area = 0;
};

struct TrackControls {
    vwave::ParameterSlider gain;
    vwave::ParameterSlider pan;
};

struct MenuItem {
    std::string label;
    std::string shortcut;
    std::function<void()> action;
    bool enabled = true;
    bool separator = false;
};

struct Menu {
    std::string title;
    SDL_Rect titleRect{};
    std::vector<MenuItem> items;
};

} // namespace vwave::ui
