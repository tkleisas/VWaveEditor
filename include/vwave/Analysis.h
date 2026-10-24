#pragma once

#include "vwave/Audio.h"
#include "vwave/Document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct Onset {
    int64_t frame = 0;
    float strength = 0.0f;
};

struct BeatAnalysis {
    double bpm = 0.0;
    double confidence = 0.0;
    std::vector<Onset> onsets;
    std::vector<int64_t> beats;
    double firstBeatSeconds = 0.0;
};

struct PitchOptions {
    double minHz = 40.0;
    double maxHz = 2000.0;
    double threshold = 0.15;
};

struct PitchEstimate {
    double frequency = 0.0;
    double confidence = 0.0;
    int64_t frame = 0;
};

struct SliceExportOptions {
    std::string prefix = "slice";
    AudioExportOptions audio;
};

struct SliceExportResult {
    std::vector<std::string> files;
    std::string manifestPath;
};

BeatAnalysis analyzeBeats(const Audio& audio, double sensitivity = 0.5);
std::vector<Onset> detectOnsets(const Audio& audio, double sensitivity = 0.5);
PitchEstimate detectPitch(const Audio& audio, Range range = {},
                          const PitchOptions& options = {});

std::vector<Slice> sliceByTransients(const Audio& audio,
                                     double sensitivity = 0.5,
                                     double minSliceSeconds = 0.03);
std::vector<Slice> sliceByBeats(const Audio& audio, double bpm,
                                int beatsPerSlice = 1);
std::vector<Slice> sliceByMarkers(const Audio& audio,
                                  const std::vector<int64_t>& markers);
bool exportSlices(const Audio& audio, const std::vector<Slice>& slices,
                  const std::string& directory,
                  const SliceExportOptions& options,
                  SliceExportResult* result = nullptr,
                  std::string* error = nullptr);

} // namespace vwave
