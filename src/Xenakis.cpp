#include "vwave/Xenakis.h"

#include <xen/clouds.h>
#include <xen/engine.h>
#include <xen/generators.h>
#include <xen/markov.h>
#include <xen/page_model.h>
#include <xen/sieve.h>
#include <xen/strategy.h>
#include <xen/tuning.h>
#include <xen/upic.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vwave {

namespace {

constexpr int kRenderBlock = 512;

xen::WaveformType waveformType(XenakisWaveform waveform) {
    switch (waveform) {
        case XenakisWaveform::Saw:      return xen::WaveformType::Saw;
        case XenakisWaveform::Square:   return xen::WaveformType::Square;
        case XenakisWaveform::Triangle: return xen::WaveformType::Triangle;
        case XenakisWaveform::Gendyn:   return xen::WaveformType::Gendyn;
        case XenakisWaveform::Sine:
        default:                        return xen::WaveformType::Sine;
    }
}

xen::Distribution distributionType(XenakisDistribution distribution) {
    switch (distribution) {
        case XenakisDistribution::Gaussian:    return xen::Distribution::Gaussian;
        case XenakisDistribution::Cauchy:      return xen::Distribution::Cauchy;
        case XenakisDistribution::Logistic:    return xen::Distribution::Logistic;
        case XenakisDistribution::Exponential: return xen::Distribution::Exponential;
        case XenakisDistribution::Uniform:
        default:                               return xen::Distribution::Uniform;
    }
}

xen::SieveMode sieveMode(XenakisSieveMode mode) {
    switch (mode) {
        case XenakisSieveMode::Field:    return xen::SieveMode::Field;
        case XenakisSieveMode::Rhythm:   return xen::SieveMode::Rhythm;
        case XenakisSieveMode::Arpeggio:
        default:                         return xen::SieveMode::Arpeggio;
    }
}

xen::Sieve sieveForPreset(int preset) {
    switch (preset) {
        case 1:
            return xen::Sieve::chromatic12();
        case 2:
            return xen::Sieve::fromOffsets(12, {0, 2, 4, 6, 8, 10});
        case 3:
            return xen::Sieve::fromOffsets(12, {0, 2, 3, 4, 6, 7, 8, 10, 11});
        case 4:
            return xen::Sieve::fromOffsets(7, {0, 2, 4, 5, 7, 9, 11});
        case 5: {
            xen::Sieve sieve;
            sieve.addClass(3, 0);
            sieve.addClass(4, 1);
            sieve.addClass(5, 2);
            return sieve;
        }
        case 0:
        default:
            return xen::Sieve::diatonic();
    }
}

xen::Tuning defaultTuning() {
    xen::Tuning tuning;
    tuning.referenceHz = 8.175798915643707;
    tuning.referenceIndex = 0.0;
    tuning.divisionsPerOctave = 12.0;
    tuning.discrete = false;
    return tuning;
}

std::string pageName(const XenakisGenerateOptions& options) {
    std::string name = xenakisGeneratorName(options.generator);
    if (options.generator == XenakisGenerator::Sieve)
        name += std::string(" ") + xenakisSieveModeName(options.sieveMode);
    return name;
}

} // namespace

const char* xenakisGeneratorName(XenakisGenerator generator) {
    switch (generator) {
        case XenakisGenerator::Markov:   return "Markov";
        case XenakisGenerator::Cloud:    return "Cloud";
        case XenakisGenerator::Strategy: return "Game";
        case XenakisGenerator::Sieve:
        default:                         return "Sieve";
    }
}

const char* xenakisWaveformName(XenakisWaveform waveform) {
    switch (waveform) {
        case XenakisWaveform::Saw:      return "Saw";
        case XenakisWaveform::Square:   return "Square";
        case XenakisWaveform::Triangle: return "Triangle";
        case XenakisWaveform::Gendyn:   return "Gendyn";
        case XenakisWaveform::Sine:
        default:                        return "Sine";
    }
}

const char* xenakisSieveModeName(XenakisSieveMode mode) {
    switch (mode) {
        case XenakisSieveMode::Field:  return "Field";
        case XenakisSieveMode::Rhythm: return "Rhythm";
        case XenakisSieveMode::Arpeggio:
        default:                       return "Arpeggio";
    }
}

const char* xenakisDistributionName(XenakisDistribution distribution) {
    switch (distribution) {
        case XenakisDistribution::Gaussian:    return "Gaussian";
        case XenakisDistribution::Cauchy:      return "Cauchy";
        case XenakisDistribution::Logistic:    return "Logistic";
        case XenakisDistribution::Exponential: return "Exponential";
        case XenakisDistribution::Uniform:
        default:                               return "Uniform";
    }
}

bool isXenakisPageFile(const std::string& path) {
    if (!std::filesystem::exists(path) ||
        !std::filesystem::is_regular_file(path))
        return false;
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".xenpage") return true;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[8] = {};
    in.read(magic, 7);
    return std::string(magic, static_cast<size_t>(in.gcount())) == "XENPAGE";
}

std::string generateXenakisPage(const XenakisGenerateOptions& options,
                                std::string* error) {
    if (error) error->clear();
    const int lowIndex = std::clamp(options.lowIndex, 0, 126);
    const int highIndex = std::clamp(options.highIndex, lowIndex + 1, 127);
    const double duration = options.duration > 0.0 ? options.duration : 16.0;
    const int group = std::clamp(options.group, 0, 3);
    const xen::WaveformType waveform = waveformType(options.waveform);
    const xen::Tuning tuning = defaultTuning();

    xen::Page page;
    page.name = pageName(options);
    page.envelopes[0] =
        options.generator == XenakisGenerator::Cloud
            ? xen::Envelope::ad(0.002f, 0.02f)
            : xen::Envelope::ad(0.005f, 0.05f);

    if (options.generator == XenakisGenerator::Sieve) {
        xen::SieveGenParams params;
        params.t0 = 0.0;
        params.t1 = duration;
        params.lowIndex = lowIndex;
        params.highIndex = highIndex;
        params.mode = sieveMode(options.sieveMode);
        params.waveform = waveform;
        params.weight = 0.5f;
        params.noteDuration = std::max(0.02, options.noteDuration);
        params.timeUnit = std::max(0.005, options.timeUnit);
        params.group = group;
        params.maxArcs = 256;
        xen::generateSieve(page, sieveForPreset(options.sievePreset), params,
                           tuning);
    } else if (options.generator == XenakisGenerator::Markov) {
        const int states = std::clamp(options.markovStates, 2, 16);
        xen::MarkovChain chain = xen::MarkovChain::randomMatrix(
            states, std::max(0.0, options.markovBias), options.seed);
        xen::MarkovGenParams params;
        params.t0 = 0.0;
        params.t1 = duration;
        params.lowIndex = lowIndex;
        params.highIndex = highIndex;
        params.noteDuration = std::max(0.02, options.noteDuration);
        params.waveform = waveform;
        params.weight = 0.5f;
        params.group = group;
        params.seed = options.seed;
        xen::generateMarkov(page, chain, params, tuning);
    } else if (options.generator == XenakisGenerator::Cloud) {
        xen::CloudGenParams params;
        params.t0 = 0.0;
        params.t1 = duration;
        params.lowIndex = lowIndex;
        params.highIndex = highIndex;
        params.waveform = waveform;
        params.group = group;
        params.cloud.density = std::clamp(options.cloudDensity, 0.2, 120.0);
        params.cloud.pitchCenter =
            std::clamp(options.cloudPitch, 20.0, 12000.0);
        params.cloud.pitchSpreadSemitones =
            std::clamp(options.cloudSpread, 0.0, 48.0);
        params.cloud.durationMean =
            std::clamp(options.cloudDuration, 0.005, 1.5);
        params.cloud.ampMean = std::clamp(options.cloudAmp, 0.0, 1.0);
        xen::Rng rng(options.seed);
        xen::generateCloudArcs(page, params, tuning, rng);
    } else {
        const int rows = std::clamp(options.strategyRows, 2, 8);
        const int cols = std::clamp(options.strategyCols, 2, 8);
        xen::Rng rng(options.seed);
        xen::StrategyGame game(rows, cols);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                game.setPayoff(row, col, rng.uniform(-1.0, 1.0),
                               rng.uniform(-1.0, 1.0));
            }
        }
        xen::StrategyGenParams params;
        params.t0 = 0.0;
        params.t1 = duration;
        params.lowIndex = lowIndex;
        params.highIndex = highIndex;
        params.segmentDuration = std::max(0.02, options.strategySegment);
        params.waveform = waveform;
        params.weight = 0.5f;
        params.group = group;
        params.seed = options.seed;
        params.useReplicator = options.strategyReplicator;
        xen::generateStrategy(page, game, params, tuning);
    }

    page.updateDuration();
    page.loopEnd = page.duration;
    if (page.arcs.empty()) {
        if (error) *error = "generator produced no arcs";
        return {};
    }
    return xen::serializePage(page);
}

Audio renderXenakisPage(const std::string& pageText,
                        const XenakisRenderOptions& options,
                        std::string* error) {
    if (error) error->clear();

    xen::Page page;
    if (!xen::deserializePage(pageText, page)) {
        if (error) *error = "invalid Xenakis page";
        return {};
    }
    if (page.arcs.empty()) {
        if (error) *error = "Xenakis page has no arcs";
        return {};
    }

    const int sampleRate = options.sampleRate > 0 ? options.sampleRate : 48000;
    const double duration =
        options.duration > 0.0 ? options.duration : page.duration;
    if (!std::isfinite(duration) || duration <= 0.0) {
        if (error) *error = "invalid Xenakis render duration";
        return {};
    }

    const int64_t frames = std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(duration * sampleRate)));
    Audio out(1, frames, sampleRate);

    xen::GendynVoice::Params gendyn;
    gendyn.segments =
        std::clamp(options.gendyn.segments, 2, xen::GendynVoice::kMaxSegments);
    gendyn.distribution = distributionType(options.gendyn.distribution);
    gendyn.durationStep =
        std::clamp(options.gendyn.durationStep, 0.0, 1.5);
    gendyn.durationBarrier =
        std::clamp(options.gendyn.durationBarrier, 0.0, 0.95);
    gendyn.ampStep = std::clamp(options.gendyn.ampStep, 0.0, 1.0);
    gendyn.ampBarrier = std::clamp(options.gendyn.ampBarrier, 0.0, 1.0);
    gendyn.mirror = options.gendyn.mirror;

    xen::Engine engine;
    engine.prepare(static_cast<double>(sampleRate), kRenderBlock);
    engine.setPage(std::make_shared<const xen::Page>(std::move(page)));
    engine.setArcGendynParams(gendyn);
    engine.setTransport(true, options.loop, 1.0, false);
    engine.setGains(0.0f, 0.0f, 0.0f);
    engine.setPlayhead(0.0);

    std::vector<float> left(kRenderBlock);
    std::vector<float> right(kRenderBlock);
    float* destination = out.channel(0);
    const float gain = static_cast<float>(std::max(0.0, options.gain));

    int64_t written = 0;
    while (written < frames) {
        const int count = static_cast<int>(
            std::min<int64_t>(kRenderBlock, frames - written));
        engine.render(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
            destination[written + i] = left[static_cast<size_t>(i)] * gain;
        written += count;
    }

    if (options.normalize) {
        float peak = 0.0f;
        for (int64_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::fabs(destination[i]));
        const float target =
            static_cast<float>(std::clamp(options.peak, 0.0, 1.0));
        if (peak > 1e-6f && target > 0.0f) {
            const float scale = target / peak;
            for (int64_t i = 0; i < frames; ++i)
                destination[i] *= scale;
        }
    }

    return out;
}

Audio renderXenakisPageFile(const std::string& path,
                            const XenakisRenderOptions& options,
                            std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open Xenakis page: " + path;
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return renderXenakisPage(buffer.str(), options, error);
}

} // namespace vwave
