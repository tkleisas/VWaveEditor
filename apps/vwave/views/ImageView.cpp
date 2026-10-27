#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace vwave::ui;

void App::drawImage(const SDL_Rect& area) {
    const auto& image = m_imageController.image();
    if (!image.valid()) {
        drawText("No image loaded", area.x + 40, area.y + 54, kTextDim,
                 fontTitle());
        drawText("Load a PNG, JPEG, BMP, TGA, GIF, PPM, PGM, or HDR image.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        drawText("Bright pixels become spectral magnitude; the image maps to time and frequency.",
                 area.x + 40, area.y + 112, kTextFaint, fontSmall());
        return;
    }
    drawText("Image to spectrogram", area.x + 20, area.y + 10, kAccent,
             fontBold());
    std::ostringstream info;
    info << image.width << " x " << image.height << "   "
         << kImageFrequencyNames[m_imageController.frequencyScale()] << " frequency   "
         << kImageTimeNames[m_imageController.timeMapping()] << " time   "
         << kImagePhaseNames[m_imageController.phaseMode()];
    drawText(info.str(), area.x + 20, area.y + 28, kTextFaint, fontSmall());

    const int availableW = area.w - 40;
    const int availableH = area.h - 72;
    const double scale = std::min(
        static_cast<double>(availableW) / std::max(1, image.width),
        static_cast<double>(availableH) / std::max(1, image.height));
    const int drawW = std::max(1, static_cast<int>(image.width * scale));
    const int drawH = std::max(1, static_cast<int>(image.height * scale));
    SDL_Rect target{area.x + (area.w - drawW) / 2,
                    area.y + 46 + (availableH - drawH) / 2, drawW, drawH};
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &target);
    if (m_imageController.texture()) {
        SDL_RenderCopy(m_renderer, m_imageController.texture(), nullptr, &target);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &target);
    drawText("time ->", target.x, target.y + target.h + 4, kTextFaint,
             fontSmall());
    drawText("^ high frequency", target.x, target.y - 14, kTextFaint,
             fontSmall());
    drawText("Brightness = magnitude; color can drive phase; alpha gates.",
             area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
}

