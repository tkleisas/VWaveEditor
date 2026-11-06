#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct ScopeXYOptions {
    int width = 512;
    int height = 512;
    Range range;
    double gain = 1.0;
    int stride = 1;
    bool connect = true;
    double intensity = 1.0;
};

struct ScopeXYPlot {
    int width = 0;
    int height = 0;
    std::vector<float> intensity;
    int64_t frames = 0;
    float peak = 0.0f;

    bool valid() const {
        return width > 0 && height > 0 &&
               intensity.size() == static_cast<size_t>(width) *
                                       static_cast<size_t>(height);
    }
    float at(int x, int y) const;
};

ScopeXYPlot renderScopeXY(const Audio& audio,
                          const ScopeXYOptions& options = {},
                          std::string* error = nullptr);

} // namespace vwave
