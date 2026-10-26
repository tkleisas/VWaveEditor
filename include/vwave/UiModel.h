#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <vector>

namespace vwave {

enum class EditorTab {
    Waveform,
    Spectrum,
    Wavetable,
    Mastering,
    Image,
    Oscilloscope
};

struct WaveformViewport {
    double startFrame = 0.0;
    double visibleFrames = 1.0;

    void fit(int64_t totalFrames);
    void clamp(int64_t totalFrames);
    double samplesPerPixel(double width) const;
    int64_t frameAt(double x, double width, int64_t totalFrames) const;
    double pixelAt(double frame, double width) const;
    void zoomAt(double factor, double x, double width, int64_t totalFrames,
                double minVisibleFrames = 16.0);
    void pan(double deltaPixels, double width, int64_t totalFrames);
};

struct SpectralViewport {
    double minHz = 20.0;
    double maxHz = 20000.0;
    bool logarithmic = true;

    double frequencyAtY(double y, double height) const;
    double yAtFrequency(double hz, double height) const;
    void zoom(double factor, double centerHz, double minLimit, double maxLimit);
    void fit(double minLimit, double maxLimit);
};

struct SpectralRegion {
    Range frames;
    double lowHz = 0.0;
    double highHz = 0.0;
    bool active = false;

    void clear() {
        frames = {};
        lowHz = 0.0;
        highHz = 0.0;
        active = false;
    }
};


struct ParameterSlider {
    double minimum = 0.0;
    double maximum = 1.0;
    double value = 0.0;
    bool logarithmic = false;
    double step = 0.0;

    double normalized() const;
    void setNormalized(double position);
    void setValue(double newValue);
};

struct PeakBin {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

std::vector<PeakBin> computePeaks(const Audio& audio, int channel, Range range,
                                  int width);

struct WavetableViewport {
    int selectedFrame = 0;
    void clamp(int frameCount);
};

} // namespace vwave
