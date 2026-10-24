#include "vwave/UiModel.h"

#include <algorithm>
#include <cmath>

namespace vwave {

void WaveformViewport::fit(int64_t totalFrames) {
    startFrame = 0.0;
    visibleFrames = std::max<double>(1.0, static_cast<double>(totalFrames));
}

void WaveformViewport::clamp(int64_t totalFrames) {
    const double total = std::max<double>(1.0, static_cast<double>(totalFrames));
    visibleFrames = std::clamp(visibleFrames, 16.0, total);
    startFrame = std::clamp(startFrame, 0.0, std::max(0.0, total - visibleFrames));
}

double WaveformViewport::samplesPerPixel(double width) const {
    return width > 0.0 ? visibleFrames / width : visibleFrames;
}

int64_t WaveformViewport::frameAt(double x, double width,
                                  int64_t totalFrames) const {
    if (width <= 0.0) return 0;
    const double frame = startFrame + x * samplesPerPixel(width);
    return std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(frame)), 0, totalFrames);
}

double WaveformViewport::pixelAt(double frame, double width) const {
    if (width <= 0.0) return 0.0;
    return (frame - startFrame) / samplesPerPixel(width);
}

void WaveformViewport::zoomAt(double factor, double x, double width,
                              int64_t totalFrames,
                              double minVisibleFrames) {
    if (width <= 0.0 || factor <= 0.0) return;
    const double total = std::max<double>(1.0, static_cast<double>(totalFrames));
    const double anchor = startFrame + x * samplesPerPixel(width);
    const double newVisible = std::clamp(
        visibleFrames / factor,
        std::min(total, std::max(1.0, minVisibleFrames)), total);
    visibleFrames = newVisible;
    startFrame = anchor - (x / width) * newVisible;
    clamp(totalFrames);
}

void WaveformViewport::pan(double deltaPixels, double width,
                           int64_t totalFrames) {
    if (width <= 0.0) return;
    startFrame += deltaPixels * samplesPerPixel(width);
    clamp(totalFrames);
}

double SpectralViewport::frequencyAtY(double y, double height) const {
    if (height <= 0.0) return minHz;
    const double position = std::clamp(1.0 - y / height, 0.0, 1.0);
    if (!logarithmic) return minHz + position * (maxHz - minHz);
    const double low = std::max(1e-6, minHz);
    const double high = std::max(low + 1e-6, maxHz);
    return low * std::pow(high / low, position);
}

double SpectralViewport::yAtFrequency(double hz, double height) const {
    if (height <= 0.0) return 0.0;
    const double low = std::max(1e-6, minHz);
    const double high = std::max(low + 1e-6, maxHz);
    const double clamped = std::clamp(hz, low, high);
    double position = 0.0;
    if (logarithmic)
        position = std::log(clamped / low) / std::log(high / low);
    else
        position = (clamped - low) / (high - low);
    return (1.0 - position) * height;
}


double ParameterSlider::normalized() const {
    if (maximum <= minimum) return 0.0;
    const double clamped = std::clamp(value, minimum, maximum);
    if (logarithmic) {
        if (minimum <= 0.0) return 0.0;
        return std::log(clamped / minimum) / std::log(maximum / minimum);
    }
    return (clamped - minimum) / (maximum - minimum);
}

void ParameterSlider::setNormalized(double position) {
    if (maximum <= minimum) return;
    const double clamped = std::clamp(position, 0.0, 1.0);
    double mapped = 0.0;
    if (logarithmic) {
        if (minimum <= 0.0) return;
        mapped = minimum * std::pow(maximum / minimum, clamped);
    } else {
        mapped = minimum + (maximum - minimum) * clamped;
    }
    setValue(mapped);
}

void ParameterSlider::setValue(double newValue) {
    if (step > 0.0) {
        newValue = minimum +
                   std::round((newValue - minimum) / step) * step;
    }
    value = std::clamp(newValue, minimum, maximum);
}

std::vector<PeakBin> computePeaks(const Audio& audio, int channel, Range range,
                                  int width) {
    std::vector<PeakBin> peaks;
    if (width <= 0 || channel < 0 || channel >= audio.numChannels()) return peaks;
    const Range selected = range.clamped(audio.numFrames());
    peaks.resize(static_cast<size_t>(width));
    if (selected.empty()) return peaks;
    const double framesPerPixel =
        static_cast<double>(selected.length()) / static_cast<double>(width);
    const float* data = audio.channel(channel);
    for (int pixel = 0; pixel < width; ++pixel) {
        const int64_t start = selected.start + static_cast<int64_t>(
            std::floor(static_cast<double>(pixel) * framesPerPixel));
        int64_t end = selected.start + static_cast<int64_t>(
            std::floor(static_cast<double>(pixel + 1) * framesPerPixel));
        if (end <= start) end = std::min<int64_t>(start + 1, selected.end);
        float minimum = 0.0f;
        float maximum = 0.0f;
        bool first = true;
        for (int64_t frame = start; frame < end; ++frame) {
            const float value = data[frame];
            if (first) {
                minimum = value;
                maximum = value;
                first = false;
            } else {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
        peaks[static_cast<size_t>(pixel)] = {minimum, maximum};
    }
    return peaks;
}

void WavetableViewport::clamp(int frameCount) {
    if (frameCount <= 0) {
        selectedFrame = 0;
        return;
    }
    selectedFrame = std::clamp(selectedFrame, 0, frameCount - 1);
}

} // namespace vwave
