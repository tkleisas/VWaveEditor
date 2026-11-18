#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>

namespace vwave {

enum class XenakisWaveform {
    Sine,
    Saw,
    Square,
    Triangle,
    Gendyn,
};

enum class XenakisDistribution {
    Uniform,
    Gaussian,
    Cauchy,
    Logistic,
    Exponential,
};

enum class XenakisGenerator {
    Sieve,
    Markov,
    Cloud,
    Strategy,
};

enum class XenakisSieveMode {
    Field,
    Arpeggio,
    Rhythm,
};

struct XenakisGendynOptions {
    int segments = 12;
    XenakisDistribution distribution = XenakisDistribution::Cauchy;
    double durationStep = 0.45;
    double durationBarrier = 0.45;
    double ampStep = 0.18;
    double ampBarrier = 0.20;
    bool mirror = false;
};

struct XenakisRenderOptions {
    int sampleRate = 48000;
    double duration = 0.0;  // 0 = use the page duration
    double gain = 1.0;
    bool normalize = true;
    double peak = 0.9;
    bool loop = false;
    XenakisGendynOptions gendyn;
};

struct XenakisGenerateOptions {
    XenakisGenerator generator = XenakisGenerator::Sieve;
    double duration = 16.0;
    int lowIndex = 36;
    int highIndex = 96;
    int group = 0;
    XenakisWaveform waveform = XenakisWaveform::Gendyn;
    int sievePreset = 0;
    XenakisSieveMode sieveMode = XenakisSieveMode::Arpeggio;
    double noteDuration = 0.25;
    double timeUnit = 0.1;
    int markovStates = 6;
    double markovBias = 1.0;
    double cloudDensity = 24.0;
    double cloudPitch = 440.0;
    double cloudSpread = 12.0;
    double cloudDuration = 0.06;
    double cloudAmp = 0.25;
    int strategyRows = 3;
    int strategyCols = 3;
    double strategySegment = 0.3;
    bool strategyReplicator = false;
    uint64_t seed = 1964;
};

bool isXenakisPageFile(const std::string& path);

std::string generateXenakisPage(const XenakisGenerateOptions& options,
                                std::string* error = nullptr);

Audio renderXenakisPage(const std::string& pageText,
                        const XenakisRenderOptions& options,
                        std::string* error = nullptr);

Audio renderXenakisPageFile(const std::string& path,
                            const XenakisRenderOptions& options,
                            std::string* error = nullptr);

const char* xenakisGeneratorName(XenakisGenerator generator);
const char* xenakisWaveformName(XenakisWaveform waveform);
const char* xenakisSieveModeName(XenakisSieveMode mode);
const char* xenakisDistributionName(XenakisDistribution distribution);

} // namespace vwave
