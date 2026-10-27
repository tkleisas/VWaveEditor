#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>

namespace vwave::ui {

SDL_Color kBackground{14, 15, 19, 255};
SDL_Color kPanel{23, 25, 32, 255};
SDL_Color kPanelRaised{31, 34, 43, 255};
SDL_Color kContent{11, 12, 16, 255};
SDL_Color kBorder{52, 57, 70, 255};
SDL_Color kText{218, 222, 234, 255};
SDL_Color kTextDim{137, 145, 163, 255};
SDL_Color kTextFaint{88, 95, 112, 255};
SDL_Color kAccent{76, 201, 176, 255};
SDL_Color kAccentDark{31, 101, 91, 255};
SDL_Color kAccentText{194, 255, 238, 255};
SDL_Color kOrange{238, 168, 76, 255};
SDL_Color kRed{224, 92, 92, 255};
SDL_Color kBlue{86, 145, 230, 255};
SDL_Color kCanvas{16, 18, 24, 255};
SDL_Color kLane{15, 17, 23, 255};
SDL_Color kLaneActive{20, 25, 30, 255};
SDL_Color kMenu{16, 18, 24, 255};
SDL_Color kDisabled{27, 29, 36, 255};

void applyPalette(const std::string& theme) {
    if (theme == "light") {
        kBackground = {238, 239, 244, 255};
        kPanel = {228, 230, 237, 255};
        kPanelRaised = {216, 219, 229, 255};
        kContent = {249, 249, 252, 255};
        kBorder = {190, 194, 206, 255};
        kText = {32, 35, 44, 255};
        kTextDim = {88, 94, 108, 255};
        kTextFaint = {130, 136, 150, 255};
        kAccent = {13, 130, 116, 255};
        kAccentDark = {205, 240, 234, 255};
        kAccentText = {18, 82, 74, 255};
        kOrange = {170, 100, 15, 255};
        kRed = {190, 45, 55, 255};
        kBlue = {50, 95, 190, 255};
        kCanvas = {252, 252, 255, 255};
        kLane = {248, 249, 253, 255};
        kLaneActive = {235, 247, 245, 255};
        kMenu = {236, 238, 244, 255};
        kDisabled = {225, 227, 234, 255};
    } else {
        kBackground = {14, 15, 19, 255};
        kPanel = {23, 25, 32, 255};
        kPanelRaised = {31, 34, 43, 255};
        kContent = {11, 12, 16, 255};
        kBorder = {52, 57, 70, 255};
        kText = {218, 222, 234, 255};
        kTextDim = {137, 145, 163, 255};
        kTextFaint = {88, 95, 112, 255};
        kAccent = {76, 201, 176, 255};
        kAccentDark = {31, 101, 91, 255};
        kAccentText = {194, 255, 238, 255};
        kOrange = {238, 168, 76, 255};
        kRed = {224, 92, 92, 255};
        kBlue = {86, 145, 230, 255};
        kCanvas = {16, 18, 24, 255};
        kLane = {15, 17, 23, 255};
        kLaneActive = {20, 25, 30, 255};
        kMenu = {16, 18, 24, 255};
        kDisabled = {27, 29, 36, 255};
    }
}

Uint32 makePixel(float normalized, float alpha) {
    const float t = std::clamp(normalized, 0.0f, 1.0f);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (t < 0.25f) {
        const float u = t / 0.25f;
        r = 7.0f + (18.0f - 7.0f) * u;
        g = 9.0f + (46.0f - 9.0f) * u;
        b = 18.0f + (92.0f - 18.0f) * u;
    } else if (t < 0.5f) {
        const float u = (t - 0.25f) / 0.25f;
        r = 18.0f + (26.0f - 18.0f) * u;
        g = 46.0f + (166.0f - 46.0f) * u;
        b = 92.0f + (171.0f - 92.0f) * u;
    } else if (t < 0.75f) {
        const float u = (t - 0.5f) / 0.25f;
        r = 26.0f + (245.0f - 26.0f) * u;
        g = 166.0f + (162.0f - 166.0f) * u;
        b = 171.0f + (52.0f - 171.0f) * u;
    } else {
        const float u = (t - 0.75f) / 0.25f;
        r = 245.0f + (255.0f - 245.0f) * u;
        g = 162.0f + (248.0f - 162.0f) * u;
        b = 52.0f + (220.0f - 52.0f) * u;
    }
    const Uint8 a = static_cast<Uint8>(
        std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return (static_cast<Uint32>(a) << 24) |
           (static_cast<Uint32>(r) << 16) |
           (static_cast<Uint32>(g) << 8) |
           static_cast<Uint32>(b);
}

SDL_Color phaseColor(float phase, Uint8 alpha) {
    constexpr double kPi = 3.14159265358979323846;
    const double twoPi = 2.0 * kPi;
    double hue = (static_cast<double>(phase) + kPi) / twoPi;
    hue -= std::floor(hue);
    const double saturation = 0.78;
    const double value = 0.98;
    const double c = value * saturation;
    const double h = hue * 6.0;
    const double x = c * (1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (h < 1.0) {
        r = c;
        g = x;
    } else if (h < 2.0) {
        r = x;
        g = c;
    } else if (h < 3.0) {
        g = c;
        b = x;
    } else if (h < 4.0) {
        g = x;
        b = c;
    } else if (h < 5.0) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const double m = value - c;
    return {static_cast<Uint8>((r + m) * 255.0),
            static_cast<Uint8>((g + m) * 255.0),
            static_cast<Uint8>((b + m) * 255.0), alpha};
}

} // namespace vwave::ui
