#include "ui/editor/TimelineMapping.h"

#include <algorithm>
#include <cmath>

namespace vwave::ui {

int timelineXForFrame(const vwave::WaveformViewport& viewport,
                      const Rect& lane, int64_t frame) {
    return lane.x + static_cast<int>(std::lround(
                        viewport.pixelAt(frame, std::max(1, lane.w))));
}

int64_t timelineFrameAtX(const vwave::WaveformViewport& viewport,
                         const Rect& lane, int x, int64_t totalFrames) {
    return viewport.frameAt(x - lane.x, std::max(1, lane.w), totalFrames);
}

int64_t timelineLooseFrameAtX(const vwave::WaveformViewport& viewport,
                              const Rect& lane, int x) {
    return std::max<int64_t>(
        0, static_cast<int64_t>(std::llround(
               viewport.startFrame +
               static_cast<double>(x - lane.x) *
                   viewport.samplesPerPixel(std::max(1, lane.w)))));
}

} // namespace vwave::ui
