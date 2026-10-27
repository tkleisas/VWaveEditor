#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace vwave::ui;

void App::drawMastering(const SDL_Rect& area) {
    if (!m_mastering.valid) {
        drawText("No mastering analysis", area.x + 40, area.y + 54,
                 kTextDim, fontTitle());
        drawText("Add aligned stems as tracks, then press Analyze Stems.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        drawText("Profiles offer different spectral-allocation goals.",
                 area.x + 40, area.y + 112, kTextFaint, fontSmall());
        return;
    }
    const int left = area.x + 18;
    const int right = area.x + area.w - 18;
    std::ostringstream summary;
    summary << m_mastering.stems.size() << " stems  |  "
            << kMasteringProfileNames[m_masteringProfile] << "  |  "
            << "Mix RMS " << formatDb(m_mastering.mixRmsDb) << "  |  "
            << "True peak " << formatDb(m_mastering.mixTruePeakDb) << "  |  "
            << "Phase " << std::fixed << std::setprecision(2)
            << m_mastering.phaseCoherence;
    drawText(summary.str(), left, area.y + 10, kTextDim, fontSmall());
    drawText("red = suggested cut    green = suggested boost    top = mix band level",
             right - 520, area.y + 10, kTextFaint, fontSmall());

    const int bandCount = std::max(1, m_mastering.bandCount);
    const int spectrumX = left;
    const int spectrumY = area.y + 36;
    const int spectrumW = right - left;
    const int spectrumH = std::max(90, static_cast<int>(area.h * 0.36));
    SDL_Rect spectrum{spectrumX, spectrumY, spectrumW, spectrumH};
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &spectrum);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &spectrum);
    const double cellWidth =
        static_cast<double>(spectrumW) / static_cast<double>(bandCount);
    for (int band = 0; band < bandCount; ++band) {
        const auto& info = m_mastering.bands[static_cast<size_t>(band)];
        const double normalized =
            std::clamp((info.mixPowerDb + 72.0) / 72.0, 0.0, 1.0);
        const int barHeight = static_cast<int>(
            normalized * static_cast<double>(spectrumH - 12));
        const int x = spectrumX +
                      static_cast<int>(std::floor(band * cellWidth));
        const int nextX = spectrumX + static_cast<int>(
                                           std::floor((band + 1) * cellWidth));
        const int width = std::max(1, nextX - x - 1);
        SDL_Rect bar{x, spectrumY + spectrumH - 6 - barHeight, width,
                     barHeight};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b,
                               170);
        SDL_RenderFillRect(m_renderer, &bar);
        const double masking =
            std::clamp(info.maskingDb / 12.0, 0.0, 1.0);
        SDL_Rect maskBar{x, spectrumY, width,
                         std::max(1, static_cast<int>(masking * 8.0))};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               160);
        SDL_RenderFillRect(m_renderer, &maskBar);
    }
    for (int band = 0; band < bandCount; ++band) {
        const auto& info = m_mastering.bands[static_cast<size_t>(band)];
        const int x = spectrumX +
                      static_cast<int>(std::floor(band * cellWidth));
        const int nextX = spectrumX + static_cast<int>(
                                           std::floor((band + 1) * cellWidth));
        const int width = std::max(1, nextX - x - 1);
        const double coherence = std::clamp(info.phaseCoherence, 0.0, 1.0);
        const int red = static_cast<int>(220.0 * (1.0 - coherence));
        const int green = static_cast<int>(180.0 * coherence + 40.0);
        SDL_SetRenderDrawColor(m_renderer, red, green, 80, 220);
        SDL_RenderDrawLine(m_renderer, x, spectrumY + spectrumH - 4,
                           x + width, spectrumY + spectrumH - 4);
    }
    for (double hz : {100.0, 1000.0, 10000.0}) {
        for (int band = 0; band < bandCount; ++band) {
            const auto& info = m_mastering.bands[static_cast<size_t>(band)];
            if (hz >= info.lowHz && hz < info.highHz) {
                const int x = spectrumX + static_cast<int>(
                                               (band + 0.5) * cellWidth);
                drawText(formatHz(hz), x - 14, spectrumY + spectrumH + 6,
                         kTextFaint, fontSmall());
                break;
            }
        }
    }

    const int rowAreaY = spectrumY + spectrumH + 30;
    const int rowAreaH = area.y + area.h - rowAreaY - 12;
    const int labelWidth = 156;
    const int rowCount = std::max(1, static_cast<int>(m_mastering.stems.size()));
    const int rowHeight =
        std::max(20, std::min(38, rowAreaH / rowCount));
    const int cellX = left + labelWidth;
    const int cellAreaW = right - cellX;
    const double gainCellWidth =
        static_cast<double>(cellAreaW) / static_cast<double>(bandCount);
    for (int stemIndex = 0; stemIndex < rowCount; ++stemIndex) {
        const auto& stem = m_mastering.stems[static_cast<size_t>(stemIndex)];
        const int y = rowAreaY + stemIndex * rowHeight;
        const SDL_Color color = stem.muted ? kTextFaint : kText;
        drawText(truncateText(stem.name, fontSmall(), labelWidth - 12),
                 left, y + 3, color, fontSmall());
        std::ostringstream levels;
        levels << "RMS " << formatDb(stem.rmsDb) << "  TP "
               << formatDb(stem.truePeakDb);
        drawText(levels.str(), left, y + 17, kTextFaint, fontSmall());
        for (int band = 0; band < bandCount; ++band) {
            const auto& info = stem.bands[static_cast<size_t>(band)];
            const int x = cellX + static_cast<int>(
                                        std::floor(band * gainCellWidth));
            const int nextX = cellX + static_cast<int>(
                                           std::floor((band + 1) *
                                                      gainCellWidth));
            SDL_Rect cell{x, y, std::max(1, nextX - x - 1),
                          std::max(10, rowHeight - 4)};
            SDL_Color fill{kPanelRaised.r, kPanelRaised.g, kPanelRaised.b,
                           255};
            if (info.suggestedGainDb < -0.05) {
                const double amount =
                    std::clamp(-info.suggestedGainDb /
                                   std::max(0.001, m_masteringMaxCut.value),
                               0.0, 1.0);
                fill = {static_cast<Uint8>(130 + 100 * amount),
                        static_cast<Uint8>(60 * (1.0 - amount)),
                        static_cast<Uint8>(60 * (1.0 - amount)), 255};
            } else if (info.suggestedGainDb > 0.05) {
                const double amount =
                    std::clamp(info.suggestedGainDb /
                                   std::max(0.001, m_masteringMaxBoost.value),
                               0.0, 1.0);
                fill = {static_cast<Uint8>(50 * (1.0 - amount)),
                        static_cast<Uint8>(110 + 90 * amount),
                        static_cast<Uint8>(60 * (1.0 - amount)), 255};
            }
            SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, 255);
            SDL_RenderFillRect(m_renderer, &cell);
            if (info.dominant) {
                SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                       kAccent.b, 180);
                SDL_RenderDrawRect(m_renderer, &cell);
            }
        }
    }
    drawText("Rows are stems; columns are logarithmic frequency bands.",
             left, area.y + area.h - 10, kTextFaint, fontSmall());
}

