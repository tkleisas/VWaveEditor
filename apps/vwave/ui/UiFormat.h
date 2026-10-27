#pragma once

#include <SDL_ttf.h>

#include <cstdint>
#include <string>

namespace vwave::ui {

std::string formatSeconds(double seconds);
std::string formatTimeInput(double seconds);
std::string formatRate(int sampleRate);
std::string formatRatio(double value);
std::string formatDb(double value);
std::string formatHz(double value);
std::string formatInt(double value);
std::string formatMilliseconds(double value);
std::string formatSecondsControl(double value);
std::string formatSemitones(double value);
std::string formatMorph(double value);
std::string formatMidiNote(double value);
std::string truncateText(const std::string& text, TTF_Font* font,
                         int maxWidth);
int64_t clampFrame(int64_t frame, int64_t frames);

} // namespace vwave::ui
