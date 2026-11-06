#include "vwave/ScopeXY.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vwave {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

void addPixel(ScopeXYPlot& plot, int x, int y, double intensity) {
    if (x < 0 || x >= plot.width || y < 0 || y >= plot.height) return;
    float& value =
        plot.intensity[static_cast<size_t>(y) * plot.width + x];
    value += static_cast<float>(intensity);
    plot.peak = std::max(plot.peak, value);
}

void drawLine(ScopeXYPlot& plot, int x0, int y0, int x1, int y1,
              double intensity) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        addPixel(plot, x0, y0, intensity);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

int mapCoordinate(double value, int size) {
    const double normalized = std::clamp(value, -1.0, 1.0);
    const double scaled = (normalized + 1.0) * 0.5 *
                          static_cast<double>(size - 1);
    return static_cast<int>(std::llround(scaled));
}

} // namespace

float ScopeXYPlot::at(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return 0.0f;
    return intensity[static_cast<size_t>(y) * width + x];
}

ScopeXYPlot renderScopeXY(const Audio& audio, const ScopeXYOptions& options,
                          std::string* error) {
    if (error) error->clear();
    ScopeXYPlot plot;
    if (audio.numChannels() < 2) {
        setError(error, "XY verification needs stereo audio");
        return plot;
    }
    if (audio.empty()) {
        setError(error, "XY verification needs non-empty audio");
        return plot;
    }
    plot.width = std::clamp(options.width, 32, 4096);
    plot.height = std::clamp(options.height, 32, 4096);
    plot.intensity.assign(static_cast<size_t>(plot.width) *
                              static_cast<size_t>(plot.height),
                          0.0f);
    const Range range = options.range.empty()
                            ? Range{0, audio.numFrames()}
                            : options.range.clamped(audio.numFrames());
    if (range.empty()) {
        setError(error, "XY verification range is empty");
        return plot;
    }
    const int stride = std::clamp(options.stride, 1, 1024);
    const double gain = std::clamp(options.gain, -16.0, 16.0);
    const double intensity =
        std::clamp(options.intensity, 0.01, 100.0);
    int previousX = 0;
    int previousY = 0;
    bool havePrevious = false;
    for (int64_t frame = range.start; frame < range.end;
         frame += stride) {
        const double left = static_cast<double>(audio.sample(0, frame)) *
                            gain;
        const double right = static_cast<double>(audio.sample(1, frame)) *
                             gain;
        const int x = mapCoordinate(left, plot.width);
        const int y = mapCoordinate(-right, plot.height);
        if (options.connect && havePrevious)
            drawLine(plot, previousX, previousY, x, y, intensity);
        else
            addPixel(plot, x, y, intensity);
        previousX = x;
        previousY = y;
        havePrevious = true;
        ++plot.frames;
    }
    return plot;
}

} // namespace vwave
