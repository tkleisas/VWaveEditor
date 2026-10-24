#include "vwave/Analysis.h"

#include "vwave/AudioIO.h"
#include "vwave/FFT.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kOnsetHop = 512;
constexpr int kOnsetFrame = 1024;

struct OnsetEnvelope {
    std::vector<float> strength;
    int hop = kOnsetHop;
};

OnsetEnvelope computeOnsetEnvelope(const Audio& audio) {
    OnsetEnvelope result;
    if (audio.empty()) return result;
    const int fftSize = nextPowerOfTwo(kOnsetFrame);
    const int frames = static_cast<int>(
        (audio.numFrames() + result.hop - 1) / result.hop);
    result.strength.assign(static_cast<size_t>(frames), 0.0f);
    if (frames <= 0) return result;

    const std::vector<float> window = hannWindow(fftSize);
    FFT fft(fftSize);
    const int bins = fftSize / 2 + 1;
    std::vector<std::vector<float>> previous(static_cast<size_t>(
                                                 audio.numChannels()),
                                             std::vector<float>(
                                                 static_cast<size_t>(bins),
                                                 0.0f));
    std::vector<float> previousEnergy(static_cast<size_t>(audio.numChannels()),
                                      0.0f);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));

    for (int frame = 0; frame < frames; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * result.hop;
        float frameFlux = 0.0f;
        for (int ch = 0; ch < audio.numChannels(); ++ch) {
            double energy = 0.0;
            for (int i = 0; i < fftSize; ++i) {
                const int64_t source = start + i;
                float value = 0.0f;
                if (source >= 0 && source < audio.numFrames()) {
                    value = audio.sample(ch, source);
                    energy += static_cast<double>(value) * value;
                }
                spectrum[static_cast<size_t>(i)] = {
                    value * window[static_cast<size_t>(i)], 0.0f};
            }
            fft.forward(spectrum);
            double flux = 0.0;
            for (int bin = 1; bin < bins; ++bin) {
                const float magnitude = std::abs(spectrum[static_cast<size_t>(bin)]);
                const float difference =
                    magnitude - previous[static_cast<size_t>(ch)][static_cast<size_t>(bin)];
                if (difference > 0.0f) flux += difference;
                previous[static_cast<size_t>(ch)][static_cast<size_t>(bin)] =
                    magnitude;
            }
            const double energyNow = std::sqrt(energy);
            const double energyRise =
                std::max(0.0, energyNow - previousEnergy[static_cast<size_t>(ch)]);
            previousEnergy[static_cast<size_t>(ch)] = energyNow;
            frameFlux += static_cast<float>(flux + energyRise * 4.0);
        }
        result.strength[static_cast<size_t>(frame)] =
            frameFlux / static_cast<float>(std::max(1, audio.numChannels()));
    }

    float peak = 0.0f;
    for (float value : result.strength) peak = std::max(peak, value);
    if (peak > 1e-12f) {
        for (float& value : result.strength) value /= peak;
    }
    return result;
}

std::vector<Onset> pickOnsets(const OnsetEnvelope& envelope,
                              double sampleRate, double sensitivity) {
    std::vector<Onset> onsets;
    const int frames = static_cast<int>(envelope.strength.size());
    if (frames <= 1 || sampleRate <= 0.0) return onsets;
    const double sensitivityClamped = std::clamp(sensitivity, 0.0, 1.0);
    const int localWindow = std::max(3, static_cast<int>(
        sampleRate / static_cast<double>(envelope.hop) * 0.15));
    const int minGapFrames = std::max(
        1, static_cast<int>(std::round(0.03 * sampleRate /
                                      static_cast<double>(envelope.hop))));
    const double factor = 1.6 - 1.1 * sensitivityClamped;

    int lastOnsetFrame = -minGapFrames * 2;
    if (frames > 2 && envelope.strength[0] > 0.02f &&
        envelope.strength[0] >= envelope.strength[1]) {
        onsets.push_back({0, envelope.strength[0]});
        lastOnsetFrame = 0;
    }
    for (int frame = 1; frame + 1 < frames; ++frame) {
        const float current = envelope.strength[static_cast<size_t>(frame)];
        if (current <= 0.02f) continue;
        if (current < envelope.strength[static_cast<size_t>(frame - 1)] ||
            current < envelope.strength[static_cast<size_t>(frame + 1)])
            continue;

        const int lo = std::max(0, frame - localWindow);
        const int hi = std::min(frames - 1, frame + localWindow);
        double sum = 0.0;
        for (int i = lo; i <= hi; ++i)
            sum += envelope.strength[static_cast<size_t>(i)];
        const double localMean =
            sum / static_cast<double>(std::max(1, hi - lo + 1));
        const double threshold = std::max(0.015, localMean * factor);
        if (current < threshold) continue;
        if (frame - lastOnsetFrame < minGapFrames) {
            if (!onsets.empty() && current > onsets.back().strength) {
                onsets.back() = {
                    static_cast<int64_t>(frame) * envelope.hop, current};
                lastOnsetFrame = frame;
            }
            continue;
        }
        onsets.push_back({static_cast<int64_t>(frame) * envelope.hop, current});
        lastOnsetFrame = frame;
    }
    return onsets;
}

double estimateTempo(const std::vector<Onset>& onsets, double sampleRate) {
    if (onsets.size() < 3) return 0.0;
    float maximumStrength = 0.0f;
    for (const Onset& onset : onsets)
        maximumStrength = std::max(maximumStrength, onset.strength);
    std::vector<Onset> accented;
    accented.reserve(onsets.size());
    for (const Onset& onset : onsets) {
        if (onset.strength >= maximumStrength * 0.3f) accented.push_back(onset);
    }
    if (accented.size() < 4) accented = onsets;

    std::vector<double> intervals;
    intervals.reserve(accented.size() - 1);
    for (size_t i = 1; i < accented.size(); ++i) {
        const double seconds = static_cast<double>(accented[i].frame -
                                                   accented[i - 1].frame) /
                               sampleRate;
        if (seconds >= 0.1 && seconds <= 2.0) intervals.push_back(seconds);
    }
    if (intervals.size() < 2) return 0.0;

    double bestBpm = 0.0;
    double bestScore = -1.0;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.25) {
        const double period = 60.0 / bpm;
        double score = 0.0;
        for (double interval : intervals) {
            double ratio = interval / period;
            double nearest = std::round(ratio);
            if (nearest < 1.0) nearest = 1.0;
            const double error = std::abs(ratio - nearest) / nearest;
            score += std::exp(-error * error * 40.0);
        }
        score /= static_cast<double>(intervals.size());
        const double octaveDistance = std::log2(bpm / 120.0) / 0.8;
        score *= std::exp(-0.5 * octaveDistance * octaveDistance);
        if (score > bestScore) {
            bestScore = score;
            bestBpm = bpm;
        }
    }
    return bestBpm;
}

std::string sanitizeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            out.push_back(c);
        else if (c == '-' || c == '_')
            out.push_back(c);
        else if (c == ' ' || c == '.')
            out.push_back('_');
    }
    if (out.empty()) out = "slice";
    return out;
}

std::vector<Slice> slicesFromBoundaries(const Audio& audio,
                                        std::vector<int64_t> boundaries) {
    std::vector<Slice> slices;
    if (audio.numFrames() <= 0) return slices;
    boundaries.push_back(0);
    boundaries.push_back(audio.numFrames());
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    boundaries.erase(
        std::remove_if(boundaries.begin(), boundaries.end(),
                       [&](int64_t value) {
                           return value <= 0 || value >= audio.numFrames();
                       }),
        boundaries.end());
    boundaries.insert(boundaries.begin(), 0);
    boundaries.push_back(audio.numFrames());
    for (size_t i = 1; i < boundaries.size(); ++i) {
        const int64_t start = boundaries[i - 1];
        const int64_t end = boundaries[i];
        if (end <= start) continue;
        std::ostringstream name;
        name << "slice_" << (slices.size() + 1);
        slices.push_back({{start, end}, name.str()});
    }
    return slices;
}

} // namespace

std::vector<Onset> detectOnsets(const Audio& audio, double sensitivity) {
    const OnsetEnvelope envelope = computeOnsetEnvelope(audio);
    return pickOnsets(envelope, audio.sampleRate(), sensitivity);
}

BeatAnalysis analyzeBeats(const Audio& audio, double sensitivity) {
    BeatAnalysis analysis;
    if (audio.empty()) return analysis;
    const OnsetEnvelope envelope = computeOnsetEnvelope(audio);
    analysis.onsets = pickOnsets(envelope, audio.sampleRate(), sensitivity);
    if (analysis.onsets.size() < 3) return analysis;

    analysis.bpm = estimateTempo(analysis.onsets, audio.sampleRate());
    if (analysis.bpm <= 0.0) return analysis;

    const double periodSamples =
        audio.sampleRate() * 60.0 / analysis.bpm;
    int64_t offset = 0;
    if (!analysis.onsets.empty()) {
        const int64_t periodFrames =
            std::max<int64_t>(1, static_cast<int64_t>(
                                     std::llround(periodSamples)));
        offset = analysis.onsets.front().frame % periodFrames;
    }
    analysis.firstBeatSeconds =
        static_cast<double>(offset) / audio.sampleRate();
    for (double position = static_cast<double>(offset);
         position < static_cast<double>(audio.numFrames());
         position += periodSamples) {
        const int64_t frame = static_cast<int64_t>(std::llround(position));
        if (frame >= 0 && frame < audio.numFrames())
            analysis.beats.push_back(frame);
    }

    const double envelopeSeconds =
        static_cast<double>(envelope.strength.size()) * envelope.hop /
        audio.sampleRate();
    double onsetDensity = static_cast<double>(analysis.onsets.size()) /
                          std::max(0.001, envelopeSeconds);
    analysis.confidence = std::clamp(
        onsetDensity * (60.0 / analysis.bpm) * 0.25, 0.0, 1.0);
    return analysis;
}

PitchEstimate detectPitch(const Audio& audio, Range range,
                          const PitchOptions& options) {
    PitchEstimate estimate;
    if (audio.empty() || options.minHz <= 0.0 || options.maxHz <= options.minHz)
        return estimate;

    Range selected = range;
    if (selected.length() <= 0) selected = {0, audio.numFrames()};
    selected = selected.clamped(audio.numFrames());
    if (selected.length() < 256) return estimate;

    const int64_t windowSize = std::min<int64_t>(selected.length(), 8192);
    const int64_t start =
        selected.start + (selected.length() - windowSize) / 2;
    std::vector<double> samples(static_cast<size_t>(windowSize), 0.0);
    double mean = 0.0;
    for (int64_t i = 0; i < windowSize; ++i) {
        double value = 0.0;
        for (int ch = 0; ch < audio.numChannels(); ++ch)
            value += audio.sample(ch, start + i);
        value /= static_cast<double>(audio.numChannels());
        samples[static_cast<size_t>(i)] = value;
        mean += value;
    }
    mean /= static_cast<double>(windowSize);
    double energy = 0.0;
    for (double& value : samples) {
        value -= mean;
        energy += value * value;
    }
    if (std::sqrt(energy / static_cast<double>(windowSize)) < 1e-5)
        return estimate;

    const double sampleRate = static_cast<double>(audio.sampleRate());
    const int tauMin = std::max(
        2, static_cast<int>(std::floor(sampleRate / options.maxHz)));
    const int tauMax = std::min(
        static_cast<int>(windowSize / 2),
        static_cast<int>(std::ceil(sampleRate / options.minHz)));
    if (tauMax <= tauMin + 2) return estimate;

    std::vector<double> difference(static_cast<size_t>(tauMax + 1), 0.0);
    for (int tau = 1; tau <= tauMax; ++tau) {
        double sum = 0.0;
        const int64_t limit = windowSize - tau;
        for (int64_t i = 0; i < limit; ++i) {
            const double delta = samples[static_cast<size_t>(i)] -
                                 samples[static_cast<size_t>(i + tau)];
            sum += delta * delta;
        }
        difference[static_cast<size_t>(tau)] = sum;
    }

    std::vector<double> normalized(static_cast<size_t>(tauMax + 1), 1.0);
    double cumulative = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau) {
        cumulative += difference[static_cast<size_t>(tau)];
        normalized[static_cast<size_t>(tau)] =
            cumulative > 1e-12
                ? difference[static_cast<size_t>(tau)] * tau / cumulative
                : 1.0;
    }

    const double threshold = std::clamp(options.threshold, 0.05, 0.5);
    int bestTau = -1;
    double bestValue = std::numeric_limits<double>::infinity();
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        const double value = normalized[static_cast<size_t>(tau)];
        if (value < threshold) {
            if (tau > tauMin &&
                normalized[static_cast<size_t>(tau - 1)] < value)
                continue;
            if (tau < tauMax &&
                normalized[static_cast<size_t>(tau + 1)] < value)
                continue;
            bestTau = tau;
            bestValue = value;
            break;
        }
    }
    if (bestTau < 0) {
        for (int tau = tauMin; tau <= tauMax; ++tau) {
            const double value = normalized[static_cast<size_t>(tau)];
            if (value < bestValue) {
                bestValue = value;
                bestTau = tau;
            }
        }
    }
    if (bestTau <= tauMin || bestTau >= tauMax - 1 || bestValue > 0.45)
        return estimate;

    double refinedTau = static_cast<double>(bestTau);
    if (bestTau > tauMin && bestTau < tauMax) {
        const double a = normalized[static_cast<size_t>(bestTau - 1)];
        const double b = normalized[static_cast<size_t>(bestTau)];
        const double c = normalized[static_cast<size_t>(bestTau + 1)];
        const double denominator = a - 2.0 * b + c;
        if (std::abs(denominator) > 1e-12)
            refinedTau += 0.5 * (a - c) / denominator;
    }
    estimate.frequency = sampleRate / refinedTau;
    estimate.confidence = std::clamp(1.0 - bestValue, 0.0, 1.0);
    estimate.frame = start;
    if (estimate.frequency < options.minHz ||
        estimate.frequency > options.maxHz)
        return PitchEstimate{};
    return estimate;
}

std::vector<Slice> sliceByTransients(const Audio& audio, double sensitivity,
                                     double minSliceSeconds) {
    std::vector<Slice> slices;
    if (audio.empty()) return slices;
    const int64_t minFrames = std::max<int64_t>(
        1, static_cast<int64_t>(minSliceSeconds * audio.sampleRate()));
    const std::vector<Onset> onsets = detectOnsets(audio, sensitivity);
    std::vector<int64_t> boundaries;
    for (const Onset& onset : onsets) {
        if (onset.frame <= 0 || onset.frame >= audio.numFrames()) continue;
        if (!boundaries.empty() &&
            onset.frame - boundaries.back() < minFrames)
            continue;
        if (audio.numFrames() - onset.frame < minFrames) continue;
        boundaries.push_back(onset.frame);
    }
    return slicesFromBoundaries(audio, std::move(boundaries));
}

std::vector<Slice> sliceByBeats(const Audio& audio, double bpm,
                                int beatsPerSlice) {
    std::vector<Slice> slices;
    if (audio.empty() || bpm <= 0.0 || beatsPerSlice <= 0) return slices;
    const double periodSamples = audio.sampleRate() * 60.0 / bpm *
                                 static_cast<double>(beatsPerSlice);
    if (periodSamples < 1.0) return slices;
    std::vector<int64_t> boundaries;
    for (double position = periodSamples;
         position < static_cast<double>(audio.numFrames());
         position += periodSamples) {
        boundaries.push_back(static_cast<int64_t>(std::llround(position)));
    }
    return slicesFromBoundaries(audio, std::move(boundaries));
}

std::vector<Slice> sliceByMarkers(const Audio& audio,
                                  const std::vector<int64_t>& markers) {
    std::vector<int64_t> boundaries;
    boundaries.reserve(markers.size());
    for (int64_t marker : markers) {
        marker = std::clamp<int64_t>(marker, 0, audio.numFrames());
        if (marker > 0 && marker < audio.numFrames())
            boundaries.push_back(marker);
    }
    return slicesFromBoundaries(audio, std::move(boundaries));
}

bool exportSlices(const Audio& audio, const std::vector<Slice>& slices,
                  const std::string& directory,
                  const SliceExportOptions& options,
                  SliceExportResult* result, std::string* error) {
    if (audio.empty()) {
        if (error) *error = "cannot export slices from empty audio";
        return false;
    }
    if (slices.empty()) {
        if (error) *error = "no slices to export";
        return false;
    }
    try {
        std::filesystem::create_directories(directory);
    } catch (const std::exception& e) {
        if (error) *error = std::string("cannot create export directory: ") + e.what();
        return false;
    }

    SliceExportResult local;
    nlohmann::json manifest;
    manifest["sampleRate"] = audio.sampleRate();
    manifest["channels"] = audio.numChannels();
    manifest["prefix"] = options.prefix;
    manifest["slices"] = nlohmann::json::array();

    for (size_t index = 0; index < slices.size(); ++index) {
        const Slice& slice = slices[index];
        const Range range = slice.range.clamped(audio.numFrames());
        if (range.empty()) {
            if (error) *error = "slice range is empty";
            return false;
        }
        const Audio sliced = audio.slice(range);
        std::ostringstream file;
        file << options.prefix << '_' << (index + 1) << '_'
             << sanitizeName(slice.name) << ".wav";
        const std::filesystem::path path =
            std::filesystem::path(directory) / file.str();
        AudioExportOptions exportOptions = options.audio;
        exportOptions.format = AudioFormat::Wav;
        if (!writeAudio(path.string(), sliced, exportOptions, error))
            return false;

        local.files.push_back(path.string());
        manifest["slices"].push_back(
            {{"name", slice.name},
             {"start", range.start},
             {"end", range.end},
             {"duration", static_cast<double>(range.length()) /
                              audio.sampleRate()},
             {"file", file.str()}});
    }

    const std::filesystem::path manifestPath =
        std::filesystem::path(directory) /
        (options.prefix + "_manifest.json");
    std::ofstream out(manifestPath, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot create slice manifest";
        return false;
    }
    out << manifest.dump(2) << '\n';
    if (!out.good()) {
        if (error) *error = "failed while writing slice manifest";
        return false;
    }
    local.manifestPath = manifestPath.string();
    if (result) *result = std::move(local);
    return true;
}

} // namespace vwave
