#pragma once

#include "vwave/Audio.h"
#include "vwave/Document.h"

#include <cstdint>

namespace vwave {

struct ResampleOptions {
    int zeroCrossings = 16;
    double rolloff = 0.95;
};

struct StretchOptions {
    int fftSize = 2048;
    int hopSize = 512;
    int wsolaFrameSize = 2048;
    int wsolaSearchRadius = 128;
};

Audio resample(const Audio& audio, int targetSampleRate,
               const ResampleOptions& options = {});
Audio resampleByRatio(const Audio& audio, double ratio,
                      const ResampleOptions& options = {});
Audio timeStretch(const Audio& audio, double ratio, StretchAlgorithm algorithm,
                  const StretchOptions& options = {});
Audio pitchShift(const Audio& audio, double semitones,
                 StretchAlgorithm algorithm, const StretchOptions& options = {});
Audio paulStretch(const Audio& audio, double ratio,
                  const StretchOptions& options = {});
Audio convolve(const Audio& audio, const Audio& impulseResponse,
               bool normalizeIr = false);
Audio applyGain(const Audio& audio, double db);
Audio normalize(const Audio& audio, double peakDb = -1.0);
int64_t findNearestZeroCrossing(const Audio& audio, int64_t frame,
                                int64_t radiusFrames);
Range snapRangeToZeroCrossings(const Audio& audio, Range range,
                               int64_t radiusFrames);

} // namespace vwave
