#pragma once

#include <SDL.h>

#include <string>

namespace vwave::ui {

extern SDL_Color kBackground;
extern SDL_Color kPanel;
extern SDL_Color kPanelRaised;
extern SDL_Color kContent;
extern SDL_Color kBorder;
extern SDL_Color kText;
extern SDL_Color kTextDim;
extern SDL_Color kTextFaint;
extern SDL_Color kAccent;
extern SDL_Color kAccentDark;
extern SDL_Color kAccentText;
extern SDL_Color kOrange;
extern SDL_Color kRed;
extern SDL_Color kBlue;
extern SDL_Color kCanvas;
extern SDL_Color kLane;
extern SDL_Color kLaneActive;
extern SDL_Color kMenu;
extern SDL_Color kDisabled;

void applyPalette(const std::string& theme);
Uint32 makePixel(float normalized, float alpha);
SDL_Color phaseColor(float phase, Uint8 alpha = 255);

} // namespace vwave::ui
