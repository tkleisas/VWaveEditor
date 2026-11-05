#pragma once

#include "ui/framework/Geometry.h"

#include "vwave/UiModel.h"

#include <cstdint>

namespace vwave::ui {

int timelineXForFrame(const vwave::WaveformViewport& viewport,
                      const Rect& lane, int64_t frame);
int64_t timelineFrameAtX(const vwave::WaveformViewport& viewport,
                         const Rect& lane, int x, int64_t totalFrames);
int64_t timelineLooseFrameAtX(const vwave::WaveformViewport& viewport,
                              const Rect& lane, int x);

} // namespace vwave::ui
