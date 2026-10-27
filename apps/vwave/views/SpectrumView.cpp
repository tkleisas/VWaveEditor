#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace vwave::ui;

void App::ensureSpectrogram() {
    if (!m_spectrogramDirty) return;
    m_spectrogramDirty = false;
    const auto audio = activeAudioSnapshot();
    if (!audio || audio->empty()) return;
    const int64_t frames = audio->numFrames();
    const int hop = static_cast<int>(
        std::max<int64_t>(256, frames / 2048));
    m_analysis.requestSpectrogram(
        audio, 1024, hop, m_spectralView.minHz, m_spectralView.maxHz,
        m_spectralView.logarithmic,
        [this](vwave::app::AnalysisService::SpectrogramRender render) {
            if (render.width <= 0 || render.height <= 0 ||
                render.pixels.empty())
                return;
            SDL_Texture* texture = SDL_CreateTexture(
                m_renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, render.width, render.height);
            if (!texture) return;
            SDL_UpdateTexture(
                texture, nullptr, render.pixels.data(),
                render.width * static_cast<int>(sizeof(Uint32)));
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            if (m_spectrogramTexture)
                SDL_DestroyTexture(m_spectrogramTexture);
            m_spectrogramTexture = texture;
            m_spectrogramTextureWidth = render.width;
            m_spectrogramTextureHeight = render.height;
            m_spectrogram = std::move(render.spectrogram);
        });
}

void App::ensurePhaseSpectrogram() {
    if (!m_phaseSpectrogramDirty) return;
    m_phaseSpectrogramDirty = false;
    const auto audio = activeAudioSnapshot();
    if (!audio || audio->empty()) {
        m_phaseSpectrogram = {};
        return;
    }
    m_analysis.requestPhase(
        audio, [this](vwave::PhaseSpectrogram phase) {
            m_phaseSpectrogram = std::move(phase);
        });
}

void App::drawSpectrum(const SDL_Rect& area) {
    if (m_spectrum3D) {
        drawSpectrum3D(area);
        return;
    }
    ensureSpectrogram();
    m_spectrumArea = {area.x + 10, area.y + 38, area.w - 20, area.h - 74};
    SDL_Rect rect = m_spectrumArea;
    SDL_SetRenderDrawColor(m_renderer, kContent.r, kContent.g, kContent.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);

    for (int i = 0; i <= 10; ++i) {
        const double frame = m_view.startFrame + m_view.visibleFrames * i / 10.0;
        const int x = rect.x + rect.w * i / 10;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 70);
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        drawText(formatSeconds(frame / std::max(1, m_timeline.sampleRate())),
                 x + 3, area.y + 5, kTextFaint, fontSmall());
    }

    if (!hasTracks() || activeDocument().audio().empty()) {
        drawText("No active track", rect.x + 20, rect.y + 20, kTextDim,
                 fontNormal());
        return;
    }
    if (!m_spectrogramTexture) {
        drawText("No spectral data", rect.x + 20, rect.y + 20, kTextDim,
                 fontNormal());
        return;
    }

    const auto& active = m_timeline.track(m_timeline.activeTrack());
    const vwave::Audio& audio = active.document.audio();
    const int64_t trackStart = active.startFrame;
    const int64_t trackFrames = audio.numFrames();
    const int64_t viewStart = static_cast<int64_t>(std::floor(m_view.startFrame));
    const int64_t viewEnd = static_cast<int64_t>(
        std::ceil(m_view.startFrame + m_view.visibleFrames));
    const int64_t localStart = std::max<int64_t>(0, viewStart - trackStart);
    const int64_t localEnd = std::min<int64_t>(trackFrames,
                                               viewEnd - trackStart);
    if (localEnd > localStart && m_spectrogramTextureWidth > 0 &&
        m_spectrogramTextureHeight > 0) {
        const double timelineStart = static_cast<double>(trackStart + localStart);
        const double timelineEnd = static_cast<double>(trackStart + localEnd);
        const int x0 = rect.x + static_cast<int>(
                                   (timelineStart - m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   rect.w);
        const int x1 = rect.x + static_cast<int>(
                                   (timelineEnd - m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   rect.w);
        SDL_Rect source{
            static_cast<int>(static_cast<double>(localStart) /
                             std::max<int64_t>(1, trackFrames) *
                             m_spectrogramTextureWidth),
            0,
            std::max(1, static_cast<int>(
                            static_cast<double>(localEnd - localStart) /
                            std::max<int64_t>(1, trackFrames) *
                            m_spectrogramTextureWidth)),
            m_spectrogramTextureHeight};
        SDL_Rect destination{x0, rect.y, std::max(1, x1 - x0), rect.h};
        SDL_RenderCopy(m_renderer, m_spectrogramTexture, &source,
                       &destination);
    }

    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);

    const double frequencies[] = {50.0,  100.0,  200.0,  500.0, 1000.0,
                                  2000.0, 5000.0, 10000.0, 20000.0};
    for (double frequency : frequencies) {
        if (frequency < m_spectralView.minHz ||
            frequency > m_spectralView.maxHz)
            continue;
        const double y = m_spectralView.yAtFrequency(frequency, rect.h);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
        SDL_RenderDrawLine(m_renderer, rect.x, rect.y + static_cast<int>(y),
                           rect.x + rect.w, rect.y + static_cast<int>(y));
        std::ostringstream label;
        label << (frequency >= 1000.0 ? frequency / 1000.0 : frequency)
              << (frequency >= 1000.0 ? "k" : "");
        drawText(label.str(), rect.x + 4,
                 rect.y + static_cast<int>(y) - 13, kTextDim, fontSmall());
    }

    if (m_loop.end() > m_loop.start()) {
        const int loopLeft = std::clamp(
            rect.x + static_cast<int>(
                         (static_cast<double>(m_loop.start()) -
                          m_view.startFrame) /
                         std::max(1.0, m_view.visibleFrames) * rect.w),
            rect.x, rect.x + rect.w);
        const int loopRight = std::clamp(
            rect.x + static_cast<int>(
                         (static_cast<double>(m_loop.end()) -
                          m_view.startFrame) /
                         std::max(1.0, m_view.visibleFrames) * rect.w),
            rect.x, rect.x + rect.w);
        SDL_Rect loop{loopLeft, rect.y, std::max(1, loopRight - loopLeft),
                      rect.h};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loop.enabled() ? 20 : 8);
        SDL_RenderFillRect(m_renderer, &loop);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loop.enabled() ? 190 : 80);
        SDL_RenderDrawLine(m_renderer, loopLeft, rect.y, loopLeft,
                           rect.y + rect.h);
        SDL_RenderDrawLine(m_renderer, loopRight, rect.y, loopRight,
                           rect.y + rect.h);
    }

    if (m_spectralController.region().active && !m_spectralController.region().frames.empty()) {
        const int x0 = rect.x + static_cast<int>(
            (static_cast<double>(trackStart + m_spectralController.region().frames.start) -
             m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * rect.w);
        const int x1 = rect.x + static_cast<int>(
            (static_cast<double>(trackStart + m_spectralController.region().frames.end) -
             m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * rect.w);
        const double highY =
            m_spectralView.yAtFrequency(m_spectralController.region().highHz, rect.h);
        const double lowY =
            m_spectralView.yAtFrequency(m_spectralController.region().lowHz, rect.h);
        SDL_Rect selection{std::min(x0, x1), rect.y + static_cast<int>(highY),
                           std::max(1, std::abs(x1 - x0)),
                           std::max(1, static_cast<int>(lowY - highY))};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 45);
        SDL_RenderFillRect(m_renderer, &selection);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 230);
        SDL_RenderDrawRect(m_renderer, &selection);
        std::ostringstream description;
        description << std::fixed << std::setprecision(0)
                    << m_spectralController.region().lowHz << " - "
                    << m_spectralController.region().highHz << " Hz";
        drawText(description.str(), selection.x + 4, selection.y - 14,
                 kAccent, fontSmall());
    }

    const int x = rect.x + static_cast<int>(
        (static_cast<double>(m_playhead) - m_view.startFrame) /
        std::max(1.0, m_view.visibleFrames) * rect.w);
    if (x >= rect.x && x <= rect.x + rect.w) {
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               255);
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
    }

    drawText("Active: " + active.name + "   " +
                 formatHz(m_spectralView.minHz) + " - " +
                 formatHz(m_spectralView.maxHz),
             rect.x + 4, rect.y + rect.h + 8, kTextFaint, fontSmall());
}

void App::drawSpectrum3D(const SDL_Rect& area) {
    ensurePhaseSpectrogram();
    const int left = area.x + 20;
    const int right = area.x + area.w - 20;
    drawText("Phase-aware 3D spectrum", left, area.y + 8, kAccent,
             fontBold());
    if (!hasTracks() || activeDocument().audio().empty()) {
        drawText("No active track", left, area.y + 54, kTextDim, fontNormal());
        return;
    }
    if (!m_phaseSpectrogram.valid()) {
        drawText("No phase data", left, area.y + 54, kTextDim, fontNormal());
        return;
    }
    const int plotTop = area.y + 38;
    const int plotBottom = area.y + area.h - 54;
    const double plotW = static_cast<double>(right - left);
    const double plotH = static_cast<double>(plotBottom - plotTop);
    const double baseX = left + plotW * 0.03;
    const double baseY = plotTop + plotH * 0.48;
    const double depthX = plotW * 0.15;
    const double depthY = plotH * 0.18;
    const double freqW = plotW * 0.78;
    const double ampH = plotH * 0.40;
    m_spectrumArea = {left, plotTop, static_cast<int>(plotW),
                      static_cast<int>(plotH)};
    SDL_Rect frame{left, plotTop, static_cast<int>(plotW),
                   static_cast<int>(plotH)};
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &frame);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &frame);

    const double maxLimit = std::min(
        20000.0, std::max(1000.0, m_timeline.sampleRate() / 2.0));
    const double minHz = std::clamp(m_spectralView.minHz, 10.0, maxLimit);
    const double maxHz =
        std::clamp(m_spectralView.maxHz, minHz + 1.0, maxLimit);
    const double logRatio = std::log(maxHz / minHz);
    const double floorDb = std::max(
        -120.0, m_phaseSpectrogram.maxDb - 72.0);
    const double spanDb = std::max(1.0, m_phaseSpectrogram.maxDb - floorDb);

    const int timeSlices = std::clamp(
        m_phaseSpectrogram.frameCount, 1, 96);
    const int columns = std::clamp(
        static_cast<int>(plotW / 3.0), 80, 260);
    struct SurfacePoint {
        int x = 0;
        int y = 0;
        SDL_Color color{};
    };
    std::vector<SurfacePoint> previousRow;
    std::vector<SurfacePoint> currentRow;
    std::vector<SDL_Vertex> mesh;
    for (int slice = 0; slice < timeSlices; ++slice) {
        const double depth = timeSlices > 1
                                 ? static_cast<double>(slice) /
                                       static_cast<double>(timeSlices - 1)
                                 : 0.0;
        const int sourceFrame = timeSlices > 1
                                    ? static_cast<int>(std::llround(
                                          depth *
                                          static_cast<double>(
                                              m_phaseSpectrogram.frameCount -
                                              1)))
                                    : 0;
        const double bx = baseX + depth * depthX;
        const double by = baseY + depth * depthY;
        currentRow.resize(static_cast<size_t>(columns));
        for (int column = 0; column < columns; ++column) {
            const double f = columns > 1
                                 ? static_cast<double>(column) /
                                       static_cast<double>(columns - 1)
                                 : 0.0;
            const double frequency = minHz * std::exp(logRatio * f);
            const int bin = std::clamp(
                static_cast<int>(std::llround(
                    frequency * m_phaseSpectrogram.fftSize /
                    std::max(1, m_phaseSpectrogram.sampleRate))),
                0, m_phaseSpectrogram.binCount - 1);
            const float db = m_phaseSpectrogram.dbAt(sourceFrame, bin);
            const float confidence =
                m_phaseSpectrogram.confidenceAt(sourceFrame, bin);
            const float phase =
                m_phaseSpectrogram.phaseAt(sourceFrame, bin);
            const double normalized =
                std::clamp((static_cast<double>(db) - floorDb) / spanDb,
                           0.0, 1.0);
            const double energyScale = std::pow(normalized, 0.65);
            const Uint8 alpha = static_cast<Uint8>(std::clamp(
                (25.0 + 150.0 * confidence) *
                    (0.08 + 0.92 * energyScale) *
                    (0.45 + 0.55 * depth),
                0.0, 235.0));
            SDL_Color color = phaseColor(phase, alpha);
            const double brightness = 0.35 + 0.65 * energyScale;
            color.r = static_cast<Uint8>(color.r * brightness);
            color.g = static_cast<Uint8>(color.g * brightness);
            color.b = static_cast<Uint8>(color.b * brightness);
            currentRow[static_cast<size_t>(column)] = {
                static_cast<int>(std::llround(bx + f * freqW)),
                static_cast<int>(std::llround(by - normalized * ampH)),
                color};
        }
        if (!previousRow.empty()) {
            mesh.clear();
            mesh.reserve(static_cast<size_t>(columns - 1) * 6);
            const Uint8 fillAlpha = static_cast<Uint8>(
                std::clamp(14.0 + 30.0 * depth, 0.0, 60.0));
            const SDL_Color fill{kPanelRaised.r, kPanelRaised.g,
                                 kPanelRaised.b, fillAlpha};
            for (int column = 0; column + 1 < columns; ++column) {
                const auto& a = previousRow[static_cast<size_t>(column)];
                const auto& b = previousRow[static_cast<size_t>(column + 1)];
                const auto& c = currentRow[static_cast<size_t>(column)];
                const auto& d = currentRow[static_cast<size_t>(column + 1)];
                mesh.push_back({SDL_FPoint{static_cast<float>(a.x),
                                           static_cast<float>(a.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(b.x),
                                           static_cast<float>(b.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(c.x),
                                           static_cast<float>(c.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(b.x),
                                           static_cast<float>(b.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(d.x),
                                           static_cast<float>(d.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
                mesh.push_back({SDL_FPoint{static_cast<float>(c.x),
                                           static_cast<float>(c.y)},
                                fill, SDL_FPoint{0.0f, 0.0f}});
            }
            if (!mesh.empty())
                SDL_RenderGeometry(m_renderer, nullptr, mesh.data(),
                                   static_cast<int>(mesh.size()), nullptr, 0);
        }
        for (int column = 0; column + 1 < columns; ++column) {
            const auto& a = currentRow[static_cast<size_t>(column)];
            const auto& b = currentRow[static_cast<size_t>(column + 1)];
            if (b.color.a < 20) continue;
            SDL_SetRenderDrawColor(m_renderer, b.color.r, b.color.g,
                                   b.color.b, b.color.a);
            SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
        }
        previousRow = currentRow;
    }

    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 160);
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX),
                       static_cast<int>(baseY),
                       static_cast<int>(baseX + freqW),
                       static_cast<int>(baseY));
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX),
                       static_cast<int>(baseY),
                       static_cast<int>(baseX + depthX),
                       static_cast<int>(baseY + depthY));
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX),
                       static_cast<int>(baseY),
                       static_cast<int>(baseX),
                       static_cast<int>(baseY - ampH));
    SDL_RenderDrawLine(m_renderer, static_cast<int>(baseX + depthX),
                       static_cast<int>(baseY + depthY),
                       static_cast<int>(baseX + depthX + freqW),
                       static_cast<int>(baseY + depthY));

    if (m_spectrum3DHoverX >= 0 && m_spectrum3DHoverY >= 0) {
        int bestDistance = 24;
        int bestFrame = -1;
        int bestBin = -1;
        int bestX = 0;
        int bestY = 0;
        for (int slice = 0; slice < timeSlices; ++slice) {
            const double depth = timeSlices > 1
                                     ? static_cast<double>(slice) /
                                           static_cast<double>(timeSlices - 1)
                                     : 0.0;
            const int sourceFrame = timeSlices > 1
                                        ? static_cast<int>(std::llround(
                                              depth *
                                              static_cast<double>(
                                                  m_phaseSpectrogram.frameCount -
                                                  1)))
                                        : 0;
            const double bx = baseX + depth * depthX;
            const double by = baseY + depth * depthY;
            for (int column = 0; column < columns; ++column) {
                const double f = columns > 1
                                     ? static_cast<double>(column) /
                                           static_cast<double>(columns - 1)
                                     : 0.0;
                const double frequency = minHz * std::exp(logRatio * f);
                const int bin = std::clamp(
                    static_cast<int>(std::llround(
                        frequency * m_phaseSpectrogram.fftSize /
                        std::max(1, m_phaseSpectrogram.sampleRate))),
                    0, m_phaseSpectrogram.binCount - 1);
                const float db = m_phaseSpectrogram.dbAt(sourceFrame, bin);
                const double normalized =
                    std::clamp((static_cast<double>(db) - floorDb) / spanDb,
                               0.0, 1.0);
                const int x = static_cast<int>(
                    std::llround(bx + f * freqW));
                const int y = static_cast<int>(std::llround(
                    by - normalized * ampH));
                const int dx = x - m_spectrum3DHoverX;
                const int dy = y - m_spectrum3DHoverY;
                const int distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestFrame = sourceFrame;
                    bestBin = bin;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        if (bestFrame >= 0 && bestBin >= 0) {
            SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                   kAccent.b, 255);
            SDL_RenderDrawLine(m_renderer, bestX - 5, bestY, bestX + 5,
                               bestY);
            SDL_RenderDrawLine(m_renderer, bestX, bestY - 5, bestX, bestY + 5);
            const float phase = m_phaseSpectrogram.phaseAt(bestFrame, bestBin);
            const float confidence =
                m_phaseSpectrogram.confidenceAt(bestFrame, bestBin);
            const float db = m_phaseSpectrogram.dbAt(bestFrame, bestBin);
            const double frequency = m_phaseSpectrogram.binFrequency(bestBin);
            const double instantaneous =
                m_phaseSpectrogram.instantaneousHzAt(bestFrame, bestBin);
            const int boxW = 248;
            const int boxH = 64;
            int boxX = std::clamp(m_spectrum3DHoverX + 12,
                                  area.x + 4, area.x + area.w - boxW - 4);
            int boxY = std::clamp(m_spectrum3DHoverY + 12,
                                  area.y + 4, area.y + area.h - boxH - 4);
            SDL_Rect box{boxX, boxY, boxW, boxH};
            SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b,
                                   240);
            SDL_RenderFillRect(m_renderer, &box);
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 255);
            SDL_RenderDrawRect(m_renderer, &box);
            std::ostringstream line1;
            line1 << formatHz(frequency) << "   " << formatDb(db)
                  << "   conf " << std::fixed << std::setprecision(2)
                  << confidence;
            std::ostringstream line2;
            line2 << "phase " << std::fixed << std::setprecision(2)
                  << phase << " rad   inst " << formatHz(instantaneous);
            drawText(line1.str(), boxX + 6, boxY + 5, kText, fontSmall());
            drawText(line2.str(), boxX + 6, boxY + 22, kTextDim, fontSmall());
            drawText("phase color; alpha = confidence", boxX + 6,
                     boxY + 39, kTextFaint, fontSmall());
        }
    }

    const int labelY = static_cast<int>(baseY + depthY + 7);
    const int maxLabelX =
        static_cast<int>(baseX + depthX + freqW);
    const std::string maxLabel = formatHz(maxHz);
    const int maxLabelWidth = textWidth(maxLabel, fontSmall());
    drawText(formatHz(minHz), static_cast<int>(baseX + depthX), labelY,
             kTextFaint, fontSmall());
    int lastLabelRight = static_cast<int>(baseX + depthX) +
                         textWidth(formatHz(minHz), fontSmall());
    for (double label : {100.0, 1000.0, 10000.0}) {
        if (label <= minHz || label >= maxHz) continue;
        const double f = std::log(label / minHz) / logRatio;
        const int x = static_cast<int>(std::llround(
            baseX + depthX + f * freqW));
        const std::string text = formatHz(label);
        const int width = textWidth(text, fontSmall());
        if (x < lastLabelRight + 8 ||
            x + width > maxLabelX - maxLabelWidth - 8)
            continue;
        drawText(text, x, labelY, kTextFaint, fontSmall());
        lastLabelRight = x + width;
    }
    drawText(maxLabel, maxLabelX - maxLabelWidth, labelY, kTextFaint,
             fontSmall());
    drawText("0 dB", left + 2, static_cast<int>(baseY - ampH) - 6,
             kTextFaint, fontSmall());
    drawText("-72 dB", left + 2, static_cast<int>(baseY) - 6, kTextFaint,
             fontSmall());
    drawText("time", static_cast<int>(baseX + depthX * 0.45),
             static_cast<int>(baseY + depthY * 0.55), kTextFaint, fontSmall());

    const int legendX = right - 250;
    const int legendY = area.y + 10;
    drawText("phase", legendX, legendY, kTextFaint, fontSmall());
    const int segments = 48;
    for (int i = 0; i < segments; ++i) {
        const float phase = -static_cast<float>(3.14159265358979323846) +
                            2.0f * static_cast<float>(3.14159265358979323846) *
                                static_cast<float>(i) /
                                static_cast<float>(segments - 1);
        const SDL_Color color = phaseColor(phase);
        SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, 255);
        SDL_Rect segment{legendX + 36 + i * 3, legendY + 1, 3, 10};
        SDL_RenderFillRect(m_renderer, &segment);
    }
    drawText("-pi", legendX + 30, legendY + 14, kTextFaint, fontSmall());
    drawText("+pi", legendX + 36 + segments * 3 - 20, legendY + 14,
             kTextFaint, fontSmall());
    std::ostringstream info;
    info << m_phaseSpectrogram.frameCount << " frames  |  FFT "
         << m_phaseSpectrogram.fftSize << "  |  "
         << m_timeline.track(m_timeline.activeTrack()).name
         << "  |  alpha = confidence";
    drawText(info.str(), left, area.y + 24, kTextFaint, fontSmall());
}

