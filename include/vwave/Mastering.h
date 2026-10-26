#pragma once

#include "vwave/Audio.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

class Timeline;

enum class MasteringProfile {
    Balanced,
    Definition,
    Separation,
    Fullness,
    Loudness,
    Custom,
};

const char* masteringProfileName(MasteringProfile profile);
bool masteringProfileFromName(const std::string& name,
                              MasteringProfile& profile);

struct MasteringOptions {
    MasteringProfile profile = MasteringProfile::Balanced;
    int fftSize = 2048;
    int hopSize = 2048;
    int bandCount = 32;
    double minHz = 20.0;
    double maxHz = 20000.0;
    double maxCutDb = -1.0;
    double maxBoostDb = -1.0;
    int smoothingBands = 1;
    bool includeMuted = false;
    double customStrength = 0.6;
    double customMaxCutDb = 10.0;
    double customMaxBoostDb = 0.0;
};

struct MasteringBand {
    double lowHz = 0.0;
    double highHz = 0.0;
    double centerHz = 0.0;
    double mixPowerDb = -120.0;
    double maskingDb = 0.0;
    double phaseCoherence = 1.0;
};

struct MasteringStemBand {
    double powerDb = -120.0;
    double shareDb = -120.0;
    double overlapDb = -120.0;
    double suggestedGainDb = 0.0;
    bool dominant = false;
};

struct MasteringStem {
    std::string name;
    int index = -1;
    bool muted = false;
    bool solo = false;
    double gainDb = 0.0;
    double pan = 0.0;
    double rmsDb = -120.0;
    double peakDb = -120.0;
    double truePeakDb = -120.0;
    double crestDb = 0.0;
    double correlation = 1.0;
    std::vector<MasteringStemBand> bands;
};

struct MasteringAnalysis {
    bool valid = false;
    MasteringProfile profile = MasteringProfile::Balanced;
    int sampleRate = 0;
    int64_t frames = 0;
    double duration = 0.0;
    int bandCount = 0;
    std::vector<MasteringBand> bands;
    std::vector<MasteringStem> stems;
    double mixRmsDb = -120.0;
    double mixPeakDb = -120.0;
    double mixTruePeakDb = -120.0;
    double mixCorrelation = 1.0;
    double phaseCoherence = 1.0;
    std::string summary;
};

MasteringAnalysis analyzeMastering(const Timeline& timeline,
                                   const MasteringOptions& options = {},
                                   std::string* error = nullptr);

struct MasteringProcessingOptions {
    int fftSize = 2048;
    int hopSize = 512;
    bool limitMix = true;
    double ceilingDb = -1.0;
};

struct MasteringProcessedStem {
    int trackIndex = -1;
    std::string name;
    Audio audio;
    double minCutDb = 0.0;
    double maxBoostDb = 0.0;
};

struct MasteringRenderResult {
    bool valid = false;
    std::vector<MasteringProcessedStem> stems;
    Audio mix;
    double appliedGainDb = 0.0;
    double mixPeakDb = -120.0;
    double mixTruePeakDb = -120.0;
    std::string summary;
};

MasteringRenderResult renderMastering(
    const Timeline& timeline, const MasteringAnalysis& analysis,
    const MasteringProcessingOptions& options = {},
    std::string* error = nullptr);
nlohmann::json masteringAnalysisToJson(const MasteringAnalysis& analysis);
bool saveMasteringAnalysis(const MasteringAnalysis& analysis,
                           const std::string& path,
                           std::string* error = nullptr);

} // namespace vwave
