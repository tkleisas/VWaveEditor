#pragma once

#include "vwave/ImageSpectrogram.h"
#include "vwave/ScopeComposition.h"

#include <cstdint>
#include <string>

namespace vwave {

struct ScopeTraceOptions {
    double threshold = 0.5;
    bool invert = false;
    bool useAlpha = true;
    double gamma = 1.0;
    double contrast = 1.0;
    int maxDimension = 1024;
    int maxStrokes = 128;
    int maxPointsPerStroke = 2048;
    double simplifyTolerance = 0.002;
    bool orderStrokes = true;
};

struct ScopeTraceResult {
    ScopeDrawing drawing;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int width = 0;
    int height = 0;
    int foregroundPixels = 0;
};

ScopeTraceResult traceImageToScope(const ImageData& image,
                                   const ScopeTraceOptions& options = {},
                                   std::string* error = nullptr);

} // namespace vwave
