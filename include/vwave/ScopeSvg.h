#pragma once

#include "vwave/ScopeComposition.h"

#include <string>

namespace vwave {

struct ScopeSvgOptions {
    int curveSegments = 16;
    int circleSegments = 64;
    double simplifyTolerance = 0.001;
    bool normalize = true;
};

bool loadScopeSvg(const std::string& file, ScopeDrawing& drawing,
                  const ScopeSvgOptions& options = {},
                  std::string* error = nullptr);
bool parseScopeSvg(const std::string& text, ScopeDrawing& drawing,
                   const ScopeSvgOptions& options = {},
                   std::string* error = nullptr);

} // namespace vwave
