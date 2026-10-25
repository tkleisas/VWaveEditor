#include "vwave/Mastering.h"

#include "vwave/FFT.h"
#include "vwave/Timeline.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <numeric>
#include <sstream>
#include <vector>

namespace vwave {

namespace {

constexpr double kSilence = 1e-20;

double powerDb(double power) {
    return std::clamp(10.0 * std::log10(std::max(power, kSilence)),
                      -120.0, 60.0);
}

struct ProfileSettings {
    double strength = 0.5;
    double maxCutDb = 6.0;
    double maxBoostDb = 0.0;
    bool fill = false;
};

struct AudioStats {
    double rmsDb = -120.0;
    double peakDb = -120.0;
    double truePeakDb = -120.0;
    double crestDb = 0.0;
    double correlation = 1.0;
};

double interpolateCubic(const float* data, int64_t frames, int64_t index,
                        double t) {
    const int64_t i0 = std::clamp<int64_t>(index - 1, 0, frames - 1);
    const int64_t i1 = std::clamp<int64_t>(index, 0, frames - 1);
    const int64_t i2 = std::clamp<int64_t>(index + 1, 0, frames - 1);
    const int64_t i3 = std::clamp<int64_t>(index + 2, 0, frames - 1);
    const double p0 = data[i0];
    const double p1 = data[i1];
    const double p2 = data[i2];
    const double p3 = data[i3];
    const double t2 = t * t;
    const double t3 = t2 * t;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                  (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

AudioStats analyzeAudioStats(const Audio& audio) {
    AudioStats stats;
    if (audio.empty()) return stats;
    const int channels = audio.numChannels();
    const int64_t frames = audio.numFrames();
    double sumSquares = 0.0;
    double peak = 0.0;
    double truePeak = 0.0;
    double sumLR = 0.0;
    double sumLL = 0.0;
    double sumRR = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const float* data = audio.channel(ch);
        for (int64_t i = 0; i < frames; ++i) {
            const double value = data[i];
            sumSquares += value * value;
            peak = std::max(peak, std::abs(value));
            truePeak = std::max(truePeak, std::abs(value));
            if (i + 1 < frames) {
                for (int sub = 1; sub < 4; ++sub) {
                    const double interpolated = interpolateCubic(
                        data, frames, i, static_cast<double>(sub) / 4.0);
                    truePeak = std::max(truePeak, std::abs(interpolated));
                }
            }
        }
    }
    if (channels >= 2) {
        const float* left = audio.channel(0);
        const float* right = audio.channel(1);
        for (int64_t i = 0; i < frames; ++i) {
            sumLR += static_cast<double>(left[i]) * right[i];
            sumLL += static_cast<double>(left[i]) * left[i];
            sumRR += static_cast<double>(right[i]) * right[i];
        }
        const double denominator = std::sqrt(sumLL * sumRR);
        stats.correlation = denominator > 1e-12 ? sumLR / denominator : 1.0;
    }
    const double meanSquare =
        sumSquares / static_cast<double>(frames * channels);
    stats.rmsDb = 10.0 * std::log10(std::max(meanSquare, kSilence));
    stats.peakDb = 20.0 * std::log10(std::max(peak, kSilence));
    stats.truePeakDb = 20.0 * std::log10(std::max(truePeak, kSilence));
    stats.crestDb = stats.truePeakDb - stats.rmsDb;
    return stats;
}

ProfileSettings profileSettings(const MasteringOptions& options) {
    ProfileSettings settings;
    switch (options.profile) {
    case MasteringProfile::Balanced:
        settings = {0.45, 6.0, 0.0, false};
        break;
    case MasteringProfile::Definition:
        settings = {0.8, 12.0, 0.0, false};
        break;
    case MasteringProfile::Separation:
        settings = {1.1, 18.0, 0.0, false};
        break;
    case MasteringProfile::Fullness:
        settings = {0.5, 8.0, 2.0, true};
        break;
    case MasteringProfile::Loudness:
        settings = {0.6, 9.0, 1.0, false};
        break;
    case MasteringProfile::Custom:
        settings = {options.customStrength, options.customMaxCutDb,
                    options.customMaxBoostDb,
                    options.customMaxBoostDb > 0.0};
        break;
    }
    settings.strength = std::clamp(settings.strength, 0.0, 2.0);
    settings.maxCutDb = std::clamp(settings.maxCutDb, 0.0, 36.0);
    settings.maxBoostDb = std::clamp(settings.maxBoostDb, 0.0, 12.0);
    if (options.maxCutDb >= 0.0) settings.maxCutDb = options.maxCutDb;
    if (options.maxBoostDb >= 0.0) settings.maxBoostDb = options.maxBoostDb;
    settings.maxCutDb = std::clamp(settings.maxCutDb, 0.0, 36.0);
    settings.maxBoostDb = std::clamp(settings.maxBoostDb, 0.0, 12.0);
    return settings;
}

std::vector<double> makeBandEdges(double minHz, double maxHz, int bands) {
    std::vector<double> edges(static_cast<size_t>(bands + 1), minHz);
    const double ratio = maxHz / minHz;
    for (int i = 0; i <= bands; ++i) {
        const double position =
            static_cast<double>(i) / static_cast<double>(bands);
        edges[static_cast<size_t>(i)] =
            minHz * std::pow(ratio, position);
    }
    edges.front() = minHz;
    edges.back() = maxHz;
    return edges;
}

int bandForFrequency(const std::vector<double>& edges, double frequency) {
    if (edges.size() < 2) return -1;
    if (frequency < edges.front() || frequency > edges.back()) return -1;
    const auto iterator =
        std::lower_bound(edges.begin(), edges.end(), frequency);
    if (iterator == edges.begin()) return 0;
    if (iterator == edges.end())
        return static_cast<int>(edges.size()) - 2;
    return static_cast<int>(iterator - edges.begin()) - 1;
}

double median(std::vector<double> values) {
    if (values.empty()) return -90.0;
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if ((values.size() & 1) != 0) return values[middle];
    return 0.5 * (values[middle - 1] + values[middle]);
}

} // namespace

const char* masteringProfileName(MasteringProfile profile) {
    switch (profile) {
    case MasteringProfile::Balanced: return "balanced";
    case MasteringProfile::Definition: return "definition";
    case MasteringProfile::Separation: return "separation";
    case MasteringProfile::Fullness: return "fullness";
    case MasteringProfile::Loudness: return "loudness";
    case MasteringProfile::Custom: return "custom";
    }
    return "balanced";
}

bool masteringProfileFromName(const std::string& name,
                              MasteringProfile& profile) {
    if (name == "balanced") profile = MasteringProfile::Balanced;
    else if (name == "definition") profile = MasteringProfile::Definition;
    else if (name == "separation") profile = MasteringProfile::Separation;
    else if (name == "fullness") profile = MasteringProfile::Fullness;
    else if (name == "loudness") profile = MasteringProfile::Loudness;
    else if (name == "custom") profile = MasteringProfile::Custom;
    else return false;
    return true;
}

MasteringAnalysis analyzeMastering(const Timeline& timeline,
                                   const MasteringOptions& options,
                                   std::string* error) {
    if (error) error->clear();
    MasteringAnalysis analysis;
    analysis.profile = options.profile;
    analysis.sampleRate = timeline.sampleRate();
    analysis.frames = timeline.lengthFrames();
    if (timeline.empty() || analysis.frames <= 0 ||
        analysis.sampleRate <= 0) {
        if (error) *error = "no timeline audio to analyze";
        return analysis;
    }

    const bool anySolo = timeline.anySolo();
    std::vector<int> stemIndices;
    for (int i = 0; i < timeline.trackCount(); ++i) {
        const auto& track = timeline.track(i);
        if (track.document.audio().empty()) continue;
        const bool audible = anySolo ? track.solo : !track.muted;
        if (!options.includeMuted && !audible) continue;
        stemIndices.push_back(i);
    }
    if (stemIndices.empty()) {
        if (error) *error = "no audible stems to analyze";
        return analysis;
    }

    const double minHz = std::max(10.0, options.minHz);
    const double maxHz = std::min(options.maxHz,
                                  static_cast<double>(analysis.sampleRate) /
                                      2.0);
    if (!std::isfinite(minHz) || !std::isfinite(maxHz) ||
        maxHz <= minHz * 1.01) {
        if (error) *error = "invalid mastering frequency range";
        return analysis;
    }
    const int bandCount = std::clamp(options.bandCount, 4, 128);
    const int fftSize = nextPowerOfTwo(
        std::clamp(options.fftSize, 512, 16384));
    const int hop = std::clamp(options.hopSize > 0 ? options.hopSize
                                                   : fftSize / 2,
                               64, fftSize);
    const int bins = fftSize / 2 + 1;
    const int frameCount = static_cast<int>(
        (analysis.frames + hop - 1) / hop);
    if (frameCount <= 0) {
        if (error) *error = "no analysis frames";
        return analysis;
    }

    const std::vector<double> edges =
        makeBandEdges(minHz, maxHz, bandCount);
    std::vector<int> binBand(static_cast<size_t>(bins), -1);
    for (int bin = 0; bin < bins; ++bin) {
        const double frequency = static_cast<double>(bin) *
                                 analysis.sampleRate / fftSize;
        binBand[static_cast<size_t>(bin)] =
            bandForFrequency(edges, frequency);
    }
    const std::vector<float> window = hannWindow(fftSize);
    double windowSum = 0.0;
    for (float value : window) windowSum += value;
    const double windowNorm = std::max(1.0, windowSum * windowSum);
    FFT fft(fftSize);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));
    const int stemCount = static_cast<int>(stemIndices.size());
    std::vector<std::vector<std::complex<float>>> monoSpectra(
        static_cast<size_t>(stemCount),
        std::vector<std::complex<float>>(static_cast<size_t>(bins)));
    std::vector<std::vector<double>> stemPower(
        static_cast<size_t>(stemCount),
        std::vector<double>(static_cast<size_t>(bandCount), 0.0));
    std::vector<double> binCoherenceNumerator(static_cast<size_t>(bins), 0.0);
    std::vector<double> binCoherenceDenominator(static_cast<size_t>(bins), 0.0);

    for (int frame = 0; frame < frameCount; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * hop;
        for (int stem = 0; stem < stemCount; ++stem) {
            const auto& track =
                timeline.track(stemIndices[static_cast<size_t>(stem)]);
            const Audio& audio = track.document.audio();
            auto& monoSpectrum =
                monoSpectra[static_cast<size_t>(stem)];
            std::fill(monoSpectrum.begin(), monoSpectrum.end(),
                      std::complex<float>{0.0f, 0.0f});
            const int channels = std::max(1, audio.numChannels());
            for (int ch = 0; ch < channels; ++ch) {
                std::fill(spectrum.begin(), spectrum.end(),
                          std::complex<float>{0.0f, 0.0f});
                for (int i = 0; i < fftSize; ++i) {
                    const int64_t timelineFrame = start + i;
                    const int64_t localFrame =
                        timelineFrame - track.startFrame;
                    if (localFrame < 0 || localFrame >= audio.numFrames())
                        continue;
                    const float value =
                        ch < audio.numChannels()
                            ? audio.sample(ch, localFrame)
                            : 0.0f;
                    spectrum[static_cast<size_t>(i)] = {
                        value * window[static_cast<size_t>(i)], 0.0f};
                }
                fft.forward(spectrum);
                for (int bin = 0; bin < bins; ++bin) {
                    monoSpectrum[static_cast<size_t>(bin)] +=
                        spectrum[static_cast<size_t>(bin)] /
                        static_cast<float>(channels);
                    const int band = binBand[static_cast<size_t>(bin)];
                    if (band < 0) continue;
                    const double power =
                        std::norm(spectrum[static_cast<size_t>(bin)]) /
                        windowNorm;
                    stemPower[static_cast<size_t>(stem)]
                             [static_cast<size_t>(band)] += power;
                }
            }
        }
        for (int bin = 0; bin < bins; ++bin) {
            std::complex<float> sum{0.0f, 0.0f};
            double magnitudeSum = 0.0;
            for (int stem = 0; stem < stemCount; ++stem) {
                const auto value =
                    monoSpectra[static_cast<size_t>(stem)]
                               [static_cast<size_t>(bin)];
                sum += value;
                magnitudeSum += std::abs(value);
            }
            if (magnitudeSum > 1e-12) {
                binCoherenceNumerator[static_cast<size_t>(bin)] +=
                    std::abs(sum);
                binCoherenceDenominator[static_cast<size_t>(bin)] +=
                    magnitudeSum;
            }
        }
    }

    analysis.valid = true;
    analysis.bandCount = bandCount;
    analysis.bands.resize(static_cast<size_t>(bandCount));
    analysis.stems.reserve(static_cast<size_t>(stemCount));
    std::vector<double> bandPower(
        static_cast<size_t>(bandCount), 0.0);
    for (int stem = 0; stem < stemCount; ++stem) {
        for (int band = 0; band < bandCount; ++band) {
            const int channels = std::max(
                1, timeline.track(stemIndices[static_cast<size_t>(stem)])
                       .document.audio()
                       .numChannels());
            stemPower[static_cast<size_t>(stem)][static_cast<size_t>(band)] /=
                static_cast<double>(frameCount * channels);
            bandPower[static_cast<size_t>(band)] +=
                stemPower[static_cast<size_t>(stem)][static_cast<size_t>(band)];
        }
    }

    double totalCoherenceNumerator = 0.0;
    double totalCoherenceDenominator = 0.0;
    for (int bin = 0; bin < bins; ++bin) {
        totalCoherenceNumerator += binCoherenceNumerator[static_cast<size_t>(bin)];
        totalCoherenceDenominator +=
            binCoherenceDenominator[static_cast<size_t>(bin)];
    }
    analysis.phaseCoherence =
        totalCoherenceDenominator > 1e-12
            ? totalCoherenceNumerator / totalCoherenceDenominator
            : 1.0;

    for (int band = 0; band < bandCount; ++band) {
        auto& result = analysis.bands[static_cast<size_t>(band)];
        result.lowHz = edges[static_cast<size_t>(band)];
        result.highHz = edges[static_cast<size_t>(band + 1)];
        result.centerHz =
            std::sqrt(result.lowHz * result.highHz);
        result.mixPowerDb = powerDb(bandPower[static_cast<size_t>(band)]);
        double coherenceNumerator = 0.0;
        double coherenceDenominator = 0.0;
        double dominantPower = 0.0;
        for (int stem = 0; stem < stemCount; ++stem) {
            const double power =
                stemPower[static_cast<size_t>(stem)][static_cast<size_t>(band)];
            dominantPower = std::max(dominantPower, power);
        }
        for (int bin = 0; bin < bins; ++bin) {
            if (binBand[static_cast<size_t>(bin)] != band) continue;
            coherenceNumerator += binCoherenceNumerator[static_cast<size_t>(bin)];
            coherenceDenominator +=
                binCoherenceDenominator[static_cast<size_t>(bin)];
        }
        result.phaseCoherence =
            coherenceDenominator > 1e-12
                ? coherenceNumerator / coherenceDenominator
                : 1.0;
        result.maskingDb =
            dominantPower > 1e-12
                ? 10.0 * std::log10(
                      std::max(bandPower[static_cast<size_t>(band)],
                               dominantPower) /
                      dominantPower)
                : 0.0;
    }

    std::vector<MasteringStem> stems(static_cast<size_t>(stemCount));
    for (int stem = 0; stem < stemCount; ++stem) {
        const auto& track =
            timeline.track(stemIndices[static_cast<size_t>(stem)]);
        auto& result = stems[static_cast<size_t>(stem)];
        result.name = track.name;
        result.index = stemIndices[static_cast<size_t>(stem)];
        result.muted = track.muted;
        result.solo = track.solo;
        result.gainDb = track.gainDb;
        result.pan = track.pan;
        const AudioStats stats = analyzeAudioStats(track.document.audio());
        result.rmsDb = stats.rmsDb;
        result.peakDb = stats.peakDb;
        result.truePeakDb = stats.truePeakDb;
        result.crestDb = stats.crestDb;
        result.correlation = stats.correlation;
        result.bands.resize(static_cast<size_t>(bandCount));
    }

    const ProfileSettings profile = profileSettings(options);
    std::vector<std::vector<double>> rawGains(
        static_cast<size_t>(stemCount),
        std::vector<double>(static_cast<size_t>(bandCount), 0.0));
    std::vector<double> activeMixDb;
    activeMixDb.reserve(static_cast<size_t>(bandCount));
    for (int band = 0; band < bandCount; ++band) {
        if (analysis.bands[static_cast<size_t>(band)].mixPowerDb > -90.0)
            activeMixDb.push_back(
                analysis.bands[static_cast<size_t>(band)].mixPowerDb);
    }
    const double medianMixDb = median(activeMixDb);
    for (int band = 0; band < bandCount; ++band) {
        const double total =
            bandPower[static_cast<size_t>(band)];
        int dominantStem = -1;
        double dominantPower = 0.0;
        for (int stem = 0; stem < stemCount; ++stem) {
            const double power =
                stemPower[static_cast<size_t>(stem)][static_cast<size_t>(band)];
            if (power > dominantPower) {
                dominantPower = power;
                dominantStem = stem;
            }
        }
        if (total <= 1e-12) {
            for (int stem = 0; stem < stemCount; ++stem) {
                stems[static_cast<size_t>(stem)]
                    .bands[static_cast<size_t>(band)]
                    .powerDb = -120.0;
            }
            continue;
        }
        const double dominantDb = powerDb(dominantPower);
        for (int stem = 0; stem < stemCount; ++stem) {
            const double power =
                stemPower[static_cast<size_t>(stem)][static_cast<size_t>(band)];
            auto& stemBand = stems[static_cast<size_t>(stem)]
                                 .bands[static_cast<size_t>(band)];
            stemBand.powerDb = powerDb(power);
            stemBand.shareDb = powerDb(power / total);
            const double other = total - power;
            stemBand.overlapDb =
                power > 1e-12 && other > 1e-12
                    ? powerDb(other / power)
                    : -120.0;
            stemBand.dominant = stem == dominantStem && power > 1e-12;
            if (power <= 1e-12) continue;
            double gain = 0.0;
            if (stem == dominantStem) {
                if (profile.fill && analysis.bands[static_cast<size_t>(band)]
                                            .mixPowerDb > -90.0) {
                    const double deficit =
                        medianMixDb -
                        analysis.bands[static_cast<size_t>(band)].mixPowerDb;
                    if (deficit > 3.0) {
                        gain = std::min(profile.maxBoostDb,
                                        (deficit - 3.0) * 0.25);
                    }
                }
            } else if (stemBand.shareDb > -24.0) {
                const double difference =
                    std::max(0.0, dominantDb - stemBand.powerDb);
                gain = -std::min(
                    profile.maxCutDb,
                    profile.strength * (difference + 2.0));
            }
            stemBand.suggestedGainDb =
                std::clamp(gain, -profile.maxCutDb, profile.maxBoostDb);
            rawGains[static_cast<size_t>(stem)][static_cast<size_t>(band)] =
                stemBand.suggestedGainDb;
        }
    }

    const int smoothing =
        std::clamp(options.smoothingBands, 0, 4);
    if (smoothing > 0) {
        for (int stem = 0; stem < stemCount; ++stem) {
            std::vector<double> smoothed(static_cast<size_t>(bandCount), 0.0);
            for (int band = 0; band < bandCount; ++band) {
                const int low = std::max(0, band - smoothing);
                const int high = std::min(bandCount - 1, band + smoothing);
                double sum = 0.0;
                int count = 0;
                for (int i = low; i <= high; ++i) {
                    sum += rawGains[static_cast<size_t>(stem)]
                                   [static_cast<size_t>(i)];
                    ++count;
                }
                smoothed[static_cast<size_t>(band)] =
                    count > 0 ? sum / static_cast<double>(count) : 0.0;
            }
            for (int band = 0; band < bandCount; ++band) {
                auto& stemBand = stems[static_cast<size_t>(stem)]
                                     .bands[static_cast<size_t>(band)];
                if (stemBand.powerDb <= -90.0) {
                    stemBand.suggestedGainDb = 0.0;
                } else {
                    stemBand.suggestedGainDb = std::clamp(
                        smoothed[static_cast<size_t>(band)],
                        -profile.maxCutDb, profile.maxBoostDb);
                }
            }
        }
    }

    analysis.stems = std::move(stems);
    analysis.duration =
        static_cast<double>(analysis.frames) / analysis.sampleRate;
    const Audio mix = timeline.mixdown();
    if (!mix.empty()) {
        const AudioStats stats = analyzeAudioStats(mix);
        analysis.mixRmsDb = stats.rmsDb;
        analysis.mixPeakDb = stats.peakDb;
        analysis.mixTruePeakDb = stats.truePeakDb;
        analysis.mixCorrelation = stats.correlation;
    } else {
        analysis.mixRmsDb = powerDb(bandPower.empty()
                                        ? 0.0
                                        : std::accumulate(
                                              bandPower.begin(),
                                              bandPower.end(), 0.0));
        analysis.mixPeakDb = analysis.mixRmsDb;
        analysis.mixTruePeakDb = analysis.mixRmsDb;
        analysis.mixCorrelation = 1.0;
    }
    int worstBand = -1;
    for (int band = 0; band < bandCount; ++band) {
        const auto& info = analysis.bands[static_cast<size_t>(band)];
        if (info.mixPowerDb <= -90.0) continue;
        if (worstBand < 0 ||
            info.maskingDb >
                analysis.bands[static_cast<size_t>(worstBand)].maskingDb)
            worstBand = band;
    }
    std::ostringstream summary;
    summary << stemCount << " stems, " << bandCount << " bands, profile "
            << masteringProfileName(options.profile);
    if (worstBand >= 0) {
        summary << "; highest overlap near "
                << static_cast<int>(std::llround(
                       analysis.bands[static_cast<size_t>(worstBand)].centerHz))
                << " Hz";
    }
    analysis.summary = summary.str();
    return analysis;
}

nlohmann::json masteringAnalysisToJson(const MasteringAnalysis& analysis) {
    nlohmann::json root;
    root["ok"] = analysis.valid;
    root["profile"] = masteringProfileName(analysis.profile);
    root["sample_rate"] = analysis.sampleRate;
    root["frames"] = analysis.frames;
    root["duration"] = analysis.duration;
    root["band_count"] = analysis.bandCount;
    root["phase_coherence"] = analysis.phaseCoherence;
    root["summary"] = analysis.summary;
    root["mix"] = {{"rms_db", analysis.mixRmsDb},
                   {"peak_db", analysis.mixPeakDb},
                   {"true_peak_db", analysis.mixTruePeakDb},
                   {"correlation", analysis.mixCorrelation}};
    root["bands"] = nlohmann::json::array();
    for (const auto& band : analysis.bands) {
        root["bands"].push_back(
            {{"low_hz", band.lowHz},
             {"high_hz", band.highHz},
             {"center_hz", band.centerHz},
             {"mix_power_db", band.mixPowerDb},
             {"masking_db", band.maskingDb},
             {"phase_coherence", band.phaseCoherence}});
    }
    root["stems"] = nlohmann::json::array();
    for (const auto& stem : analysis.stems) {
        nlohmann::json item;
        item["index"] = stem.index;
        item["name"] = stem.name;
        item["muted"] = stem.muted;
        item["solo"] = stem.solo;
        item["gain_db"] = stem.gainDb;
        item["pan"] = stem.pan;
        item["rms_db"] = stem.rmsDb;
        item["peak_db"] = stem.peakDb;
        item["true_peak_db"] = stem.truePeakDb;
        item["crest_db"] = stem.crestDb;
        item["correlation"] = stem.correlation;
        item["bands"] = nlohmann::json::array();
        for (const auto& band : stem.bands) {
            item["bands"].push_back(
                {{"power_db", band.powerDb},
                 {"share_db", band.shareDb},
                 {"overlap_db", band.overlapDb},
                 {"suggested_gain_db", band.suggestedGainDb},
                 {"dominant", band.dominant}});
        }
        root["stems"].push_back(std::move(item));
    }
    return root;
}

bool saveMasteringAnalysis(const MasteringAnalysis& analysis,
                           const std::string& path,
                           std::string* error) {
    if (error) error->clear();
    if (!analysis.valid) {
        if (error) *error = "cannot save an empty mastering analysis";
        return false;
    }
    try {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            if (error) *error = "cannot create mastering report";
            return false;
        }
        out << masteringAnalysisToJson(analysis).dump(2) << '\n';
        if (!out.good()) {
            if (error) *error = "failed while writing mastering report";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("cannot save mastering report: ") +
                            e.what();
        return false;
    }
}

} // namespace vwave
