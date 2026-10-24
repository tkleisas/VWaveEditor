#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>

namespace vwave {

struct SweepOptions {
    double startHz = 20.0;
    double endHz = 20000.0;
    double seconds = 5.0;
    double amplitude = 0.5;
    int sampleRate = 48000;
    int channels = 1;
};

struct DeconvolutionOptions {
    int64_t irLength = 0;
    bool trimToPeak = false;
    double preDelaySeconds = 0.0;
    bool normalize = true;
};

struct CaptureInfo {
    std::string name;
    std::string source;
    std::string notes;
    SweepOptions sweep;
    int sampleRate = 48000;
    int64_t frames = 0;
};

Audio generateSweep(const SweepOptions& options);
Audio extractImpulseResponse(const Audio& recorded, const Audio& sweep,
                             const DeconvolutionOptions& options = {},
                             std::string* error = nullptr);
bool writeCaptureManifest(const std::string& path, const CaptureInfo& info,
                          std::string* error = nullptr);

} // namespace vwave
