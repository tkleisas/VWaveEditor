#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>

namespace vwave {

struct XenakisRenderOptions {
    int sampleRate = 48000;
    double duration = 0.0;  // 0 = use the page duration
    double gain = 1.0;
    bool normalize = true;
    double peak = 0.9;
    bool loop = false;
};

bool isXenakisPageFile(const std::string& path);

Audio renderXenakisPage(const std::string& pageText,
                        const XenakisRenderOptions& options,
                        std::string* error = nullptr);

Audio renderXenakisPageFile(const std::string& path,
                            const XenakisRenderOptions& options,
                            std::string* error = nullptr);

} // namespace vwave
