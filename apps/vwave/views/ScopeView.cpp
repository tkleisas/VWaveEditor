#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include "vwave/ScopeXY.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace vwave::ui;

void App::drawOscilloscope(const SDL_Rect& area) {
    m_scopeTimelineArea = {};
    if (m_scopeXYMode) {
        drawText("XY audio verification", area.x + 20, area.y + 10, kAccent,
                 fontBold());
        if (!hasTracks() || activeDocument().audio().empty()) {
            drawText("Open a stereo track to verify its XY image.",
                     area.x + 20, area.y + 40, kTextFaint, fontNormal());
            return;
        }
        const auto& audio = activeDocument().audio();
        if (audio.numChannels() < 2) {
            drawText("Select a stereo track for XY verification.",
                     area.x + 20, area.y + 40, kTextFaint, fontNormal());
            return;
        }
        const int availableW = area.w - 40;
        const int availableH = area.h - 78;
        const int side = std::clamp(std::min(availableW, availableH), 160,
                                    std::max(160, availableH));
        SDL_Rect rect{area.x + (area.w - side) / 2, area.y + 46, side, side};
        const int trackIndex = m_timeline.activeTrack();
        const auto& track = m_timeline.track(trackIndex);
        const uint64_t revision = activeDocument().revision();
        const int64_t localPlayhead = std::clamp<int64_t>(
            m_playhead - track.startFrame, 0, audio.numFrames());
        const double windowSeconds =
            std::clamp(m_scopeXYWindowMs.value / 1000.0, 0.001, 0.5);
        const int64_t windowFrames = std::max<int64_t>(
            2, static_cast<int64_t>(std::llround(
                   windowSeconds * static_cast<double>(audio.sampleRate()))));
        const int64_t windowEnd = std::max<int64_t>(1, localPlayhead);
        const int64_t windowStart = std::max<int64_t>(0, windowEnd - windowFrames);
        const int64_t windowLength = windowEnd - windowStart;
        if (m_scopeXYDirty || !m_scopeXYTexture ||
            m_scopeXYTextureSize != side || m_scopeXYTrack != trackIndex ||
            m_scopeXYRevision != revision ||
            m_scopeXYWindowStart != windowStart ||
            m_scopeXYWindowFrames != windowLength) {
            if (m_scopeXYTexture) {
                SDL_DestroyTexture(m_scopeXYTexture);
                m_scopeXYTexture = nullptr;
            }
            vwave::ScopeXYOptions options;
            options.width = side;
            options.height = side;
            options.connect = true;
            options.range = {windowStart, windowEnd};
            options.stride = std::max<int>(
                1, static_cast<int>(windowLength /
                                    (static_cast<int64_t>(side) * side * 8)));
            std::string error;
            const vwave::ScopeXYPlot plot =
                vwave::renderScopeXY(audio, options, &error);
            if (plot.valid()) {
                std::vector<uint32_t> pixels(
                    static_cast<size_t>(side) * static_cast<size_t>(side),
                    0);
                for (int y = 0; y < side; ++y) {
                    for (int x = 0; x < side; ++x) {
                        const float value = plot.at(x, y);
                        if (value <= 0.0f) continue;
                        double brightness = 1.0;
                        if (plot.peak > 1e-6f) {
                            brightness = std::sqrt(
                                std::clamp(static_cast<double>(value) /
                                               static_cast<double>(plot.peak),
                                           0.0, 1.0));
                        }
                        const uint32_t alpha = static_cast<uint32_t>(
                            std::clamp(brightness, 0.0, 1.0) * 255.0);
                        pixels[static_cast<size_t>(y) * side + x] =
                            (alpha << 24) |
                            (static_cast<uint32_t>(kAccent.b) << 16) |
                            (static_cast<uint32_t>(kAccent.g) << 8) |
                            static_cast<uint32_t>(kAccent.r);
                    }
                }
                m_scopeXYTexture = SDL_CreateTexture(
                    m_renderer, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STATIC, side, side);
                if (m_scopeXYTexture) {
                    SDL_SetTextureBlendMode(m_scopeXYTexture,
                                           SDL_BLENDMODE_BLEND);
                    SDL_UpdateTexture(m_scopeXYTexture, nullptr,
                                      pixels.data(),
                                      side * static_cast<int>(sizeof(uint32_t)));
                    m_scopeXYTextureSize = side;
                    m_scopeXYTrack = trackIndex;
                    m_scopeXYRevision = revision;
                    m_scopeXYWindowStart = windowStart;
                    m_scopeXYWindowFrames = windowLength;
                }
            }
            m_scopeXYDirty = false;
        }
        SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b,
                               255);
        SDL_RenderFillRect(m_renderer, &rect);
        if (m_scopeXYTexture) {
            SDL_RenderCopy(m_renderer, m_scopeXYTexture, nullptr, &rect);
        }
        const int64_t sampleFrame = std::max<int64_t>(0, windowEnd - 1);
        const int currentX = rect.x + static_cast<int>(std::llround(
            (std::clamp<double>(audio.sample(0, sampleFrame), -1.0, 1.0) +
             1.0) * 0.5 * rect.w));
        const int currentY = rect.y + static_cast<int>(std::llround(
            (1.0 - (std::clamp<double>(audio.sample(1, sampleFrame), -1.0,
                                      1.0) + 1.0) * 0.5) * rect.h));
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               255);
        SDL_Rect dot{currentX - 2, currentY - 2, 5, 5};
        SDL_RenderFillRect(m_renderer, &dot);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
        for (int i = 1; i < 8; ++i) {
            const int x = rect.x + rect.w * i / 8;
            const int y = rect.y + rect.h * i / 8;
            SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
            SDL_RenderDrawLine(m_renderer, rect.x, y, rect.x + rect.w, y);
        }
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b,
                               210);
        SDL_RenderDrawLine(m_renderer, rect.x, rect.y + rect.h / 2,
                           rect.x + rect.w, rect.y + rect.h / 2);
        SDL_RenderDrawLine(m_renderer, rect.x + rect.w / 2, rect.y,
                           rect.x + rect.w / 2, rect.y + rect.h);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b,
                               255);
        SDL_RenderDrawRect(m_renderer, &rect);
        std::ostringstream info;
        info << track.name << "  t "
             << formatSeconds(static_cast<double>(windowEnd) /
                              static_cast<double>(audio.sampleRate()))
             << "  window " << formatMilliseconds(windowSeconds * 1000.0)
             << "  L/R";
        drawText(info.str(), rect.x, rect.y + rect.h + 12, kTextFaint,
                 fontSmall());
        drawText("Live XY window at the playhead; seek or play to advance.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
        return;
    }
    const auto& path = m_scopeController.path();
    if (!path.valid()) {
        drawText("No scope path", area.x + 40, area.y + 54, kTextDim,
                 fontTitle());
        drawText("Choose a generated shape, load a .vscope path, or clear the path and click to draw.",
                 area.x + 40, area.y + 90, kTextFaint, fontNormal());
        return;
    }
    drawText("Oscilloscope path", area.x + 20, area.y + 10, kAccent,
             fontBold());
    std::ostringstream info;
    info << path.points.size() << " points  "
         << (path.closed ? "closed" : "open") << "  "
         << kScopeShapeNames[m_scopeController.shape()] << "  scan "
         << formatHz(m_scopeScanHz.value);
    drawText(info.str(), area.x + 20, area.y + 28, kTextFaint, fontSmall());

    const int availableW = area.w - 40;
    const int availableH = area.h - 172;
    const int side = std::clamp(std::min(availableW, availableH), 160,
                                std::max(160, availableH));
    SDL_Rect rect{area.x + (area.w - side) / 2, area.y + 46, side, side};
    m_scopeArea = rect;
    SDL_SetRenderDrawColor(m_renderer, kCanvas.r, kCanvas.g, kCanvas.b, 255);
    SDL_RenderFillRect(m_renderer, &rect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
    for (int i = 1; i < 8; ++i) {
        const int x = rect.x + rect.w * i / 8;
        const int y = rect.y + rect.h * i / 8;
        SDL_RenderDrawLine(m_renderer, x, rect.y, x, rect.y + rect.h);
        SDL_RenderDrawLine(m_renderer, rect.x, y, rect.x + rect.w, y);
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 210);
    SDL_RenderDrawLine(m_renderer, rect.x, rect.y + rect.h / 2,
                       rect.x + rect.w, rect.y + rect.h / 2);
    SDL_RenderDrawLine(m_renderer, rect.x + rect.w / 2, rect.y,
                       rect.x + rect.w / 2, rect.y + rect.h);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &rect);

    vwave::ScopeTransform previewTransform;
    if (!m_scopeController.pointEditing() &&
        !m_scopeController.keyframes().empty()) {
        previewTransform =
            m_scopeController.transformAt(m_scopeKeyTime.value);
    }
    auto mapPoint = [&](const vwave::ScopePoint& source) {
        const vwave::ScopePoint transformed =
            m_scopeController.pointEditing() ||
                    m_scopeController.keyframes().empty()
                ? source
                : vwave::applyScopeTransform(previewTransform, source);
        const double x = std::clamp(transformed.x, -1.0, 1.0);
        const double y = std::clamp(transformed.y, -1.0, 1.0);
        return SDL_Point{
            rect.x + static_cast<int>(std::llround((x + 1.0) * 0.5 * rect.w)),
            rect.y + static_cast<int>(std::llround((1.0 - y) * 0.5 * rect.h))};
    };
    SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 240);
    for (size_t i = 1; i < path.points.size(); ++i) {
        const SDL_Point a = mapPoint(path.points[i - 1]);
        const SDL_Point b = mapPoint(path.points[i]);
        SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
    }
    if (path.closed && path.points.size() > 2) {
        const SDL_Point a = mapPoint(path.points.back());
        const SDL_Point b = mapPoint(path.points.front());
        SDL_RenderDrawLine(m_renderer, a.x, a.y, b.x, b.y);
    }
    if (m_scopeController.pointEditing()) {
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               230);
        for (const auto& point : path.points) {
            const SDL_Point p = mapPoint(point);
            SDL_Rect dot{p.x - 2, p.y - 2, 4, 4};
            SDL_RenderFillRect(m_renderer, &dot);
        }
    }
    drawText("x", rect.x + rect.w + 6, rect.y + rect.h / 2 - 6, kTextFaint,
             fontSmall());
    drawText("y", rect.x + rect.w / 2 - 3, rect.y - 16, kTextFaint,
             fontSmall());

    m_scopeTimelineArea = {area.x + 20, area.y + area.h - 128,
                           area.w - 40, 72};
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_scopeTimelineArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 180);
    SDL_RenderDrawRect(m_renderer, &m_scopeTimelineArea);
    const int timelineLeft = m_scopeTimelineArea.x + 10;
    const int timelineRight =
        m_scopeTimelineArea.x + m_scopeTimelineArea.w - 10;
    const int timelineY = m_scopeTimelineArea.y + 40;
    const double duration = std::max(0.01, m_scopeDuration.value);
    auto timeX = [&](double time) {
        const double fraction = std::clamp(time / duration, 0.0, 1.0);
        return timelineLeft + static_cast<int>(std::llround(
                                  fraction * (timelineRight - timelineLeft)));
    };
    if (hasTracks()) {
        const auto& track = m_timeline.track(m_timeline.activeTrack());
        const double sampleRate = static_cast<double>(
            std::max(1, m_timeline.sampleRate()));
        if (m_loop.active(m_timeline.lengthFrames())) {
            const double loopStart =
                static_cast<double>(m_loop.start() - track.startFrame) /
                sampleRate;
            const double loopEnd =
                static_cast<double>(m_loop.end() - track.startFrame) /
                sampleRate;
            const int x1 = timeX(loopStart);
            const int x2 = timeX(loopEnd);
            if (x2 > x1) {
                SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                       kAccent.b, 55);
                SDL_Rect loop{x1, timelineY - 18, x2 - x1, 36};
                SDL_RenderFillRect(m_renderer, &loop);
                SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                       kAccent.b, 180);
                SDL_RenderDrawLine(m_renderer, x1, timelineY - 18, x1,
                                   timelineY + 18);
                SDL_RenderDrawLine(m_renderer, x2, timelineY - 18, x2,
                                   timelineY + 18);
            }
        }
    }
    drawText("KEYFRAMES", m_scopeTimelineArea.x + 8,
             m_scopeTimelineArea.y + 3, kTextFaint, fontSmall());
    const std::string summary =
        std::to_string(m_scopeController.keyframes().size()) + " keys  " +
        formatSeconds(duration);
    drawText(summary, timelineRight - textWidth(summary, fontSmall()),
             m_scopeTimelineArea.y + 3, kTextFaint, fontSmall());

    double tickStep = duration / 6.0;
    const double magnitude =
        std::pow(10.0, std::floor(std::log10(std::max(1e-6, tickStep))));
    const double normalized = tickStep / magnitude;
    tickStep = (normalized <= 1.0 ? 1.0
                                   : normalized <= 2.0 ? 2.0
                                   : normalized <= 5.0 ? 5.0
                                                       : 10.0) *
               magnitude;
    int lastLabelRight = -1000;
    for (double tick = 0.0; tick <= duration + 1e-9; tick += tickStep) {
        const int x = timeX(tick);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b,
                               160);
        SDL_RenderDrawLine(m_renderer, x, timelineY + 7, x, timelineY + 12);
        const std::string label = formatSeconds(tick);
        const int width = textWidth(label, fontSmall());
        if (x - width / 2 > lastLabelRight + 6) {
            drawText(label, x - width / 2, m_scopeTimelineArea.y + 53,
                     kTextFaint, fontSmall());
            lastLabelRight = x + width / 2;
        }
    }
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 200);
    SDL_RenderDrawLine(m_renderer, timelineLeft, timelineY, timelineRight,
                       timelineY);
    const auto& keyframes = m_scopeController.keyframes();
    for (size_t i = 0; i < keyframes.size(); ++i) {
        const int x = timeX(keyframes[i].time);
        const bool selected =
            static_cast<int>(i) == m_scopeController.selectedKeyframe();
        SDL_SetRenderDrawColor(m_renderer,
                               selected ? kAccent.r : kTextDim.r,
                               selected ? kAccent.g : kTextDim.g,
                               selected ? kAccent.b : kTextDim.b, 255);
        SDL_Rect marker{x - 4, timelineY - 12, 9, 25};
        SDL_RenderFillRect(m_renderer, &marker);
    }
    const int keyX = timeX(m_scopeKeyTime.value);
    SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b, 220);
    SDL_RenderDrawLine(m_renderer, keyX, timelineY - 15, keyX, timelineY + 17);
    if (keyframes.empty()) {
        drawText("Click to set key time; Add Key inserts. Shift-drag sets loop.",
                 timelineLeft, m_scopeTimelineArea.y + 20, kTextFaint,
                 fontSmall());
    } else {
        drawText("Click key; double-click adds; Shift-drag sets loop.",
                 timelineLeft, m_scopeTimelineArea.y + 20, kTextFaint,
                 fontSmall());
    }

    if (m_scopeController.pointEditing()) {
        drawText("Click to add points; click near the first point to close.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
    } else {
        drawText("Generated path. Choose Polyline to draw by hand.",
                 area.x + 20, area.y + area.h - 18, kTextFaint, fontSmall());
    }
}

