#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vwave {

struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 && channels > 0 &&
               pixels.size() >= static_cast<size_t>(width) *
                                    static_cast<size_t>(height) *
                                    static_cast<size_t>(channels);
    }
    uint8_t at(int x, int y, int channel) const;
};

bool loadImageFile(const std::string& path, ImageData& out,
                   std::string* error = nullptr);

enum class ImageFrequencyScale {
    Linear,
    Logarithmic,
};

enum class ImageTimeMapping {
    Direct,
    Mirrored,
    Looped,
};

enum class ImagePhaseMode {
    GriffinLim,
    FastGriffinLim,
    PhaseLocked,
    Random,
    Color,
};

const char* imageFrequencyScaleName(ImageFrequencyScale scale);
bool imageFrequencyScaleFromName(const std::string& name,
                                 ImageFrequencyScale& scale);
const char* imageTimeMappingName(ImageTimeMapping mapping);
bool imageTimeMappingFromName(const std::string& name,
                              ImageTimeMapping& mapping);
const char* imagePhaseModeName(ImagePhaseMode mode);
bool imagePhaseModeFromName(const std::string& name, ImagePhaseMode& mode);

struct ImageSpectrogramOptions {
    int fftSize = 2048;
    int hopSize = 512;
    int sampleRate = 48000;
    double duration = 0.0;
    double minHz = 20.0;
    double maxHz = 20000.0;
    ImageFrequencyScale frequencyScale = ImageFrequencyScale::Logarithmic;
    ImageTimeMapping timeMapping = ImageTimeMapping::Direct;
    int loops = 1;
    double cropLeft = 0.0;
    double cropRight = 1.0;
    double cropTop = 0.0;
    double cropBottom = 1.0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool invert = false;
    double gamma = 1.0;
    double contrast = 1.0;
    double threshold = 0.0;
    bool useAlpha = true;
    ImagePhaseMode phaseMode = ImagePhaseMode::FastGriffinLim;
    int iterations = 24;
    double momentum = 0.99;
    uint32_t seed = 1;
    int channels = 1;
    bool normalize = true;
    double peak = 0.9;
    std::function<bool()> cancelled;
    std::function<void(double)> progress;
};

struct ImageSpectrogramResult {
    Audio audio;
    int imageWidth = 0;
    int imageHeight = 0;
    int fftSize = 0;
    int hopSize = 0;
    int frameCount = 0;
    int binCount = 0;
    int channels = 0;
    double duration = 0.0;
};

ImageSpectrogramResult imageToSpectrogram(
    const ImageData& image, const ImageSpectrogramOptions& options = {},
    std::string* error = nullptr);

} // namespace vwave
