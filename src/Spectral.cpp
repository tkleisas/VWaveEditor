#include "vwave/Spectral.h"

#include "vwave/FFT.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool resolveRange(const Audio& audio, Range requested, Range& selected) {
    if (audio.empty()) return false;
    if (requested.length() > 0) {
        selected = requested.clamped(audio.numFrames());
        return !selected.empty();
    }
    if (requested.start == 0 && requested.end == 0) {
        selected = {0, audio.numFrames()};
        return true;
    }
    return false;
}

int normalizedFftSize(int requested) {
    return nextPowerOfTwo(std::max(256, requested));
}

int normalizedHop(int requested, int fftSize) {
    return std::clamp(requested, 16, fftSize / 2);
}

using SpectrumProcessor = std::function<void(
    std::vector<std::complex<float>>& spectrum, int channel, int frame)>;

Audio processSpectralRange(const Audio& audio, Range range, int requestedFft,
                           int requestedHop,
                           const SpectrumProcessor& processor) {
    Range selected;
    if (!resolveRange(audio, range, selected)) return Audio{};
    if (!processor) return audio;
    const int fftSize = normalizedFftSize(requestedFft);
    const int hop = normalizedHop(requestedHop, fftSize);
    const int bins = fftSize / 2 + 1;
    const int channels = audio.numChannels();
    const int64_t length = selected.length();
    const int frameCount = static_cast<int>((length + hop - 1) / hop);
    const std::vector<float> window = hannWindow(fftSize);
    FFT fft(fftSize);

    Audio processed(channels, length, audio.sampleRate());
    std::vector<double> windowSum(static_cast<size_t>(length), 0.0);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));

    for (int frame = 0; frame < frameCount; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * hop;
        for (int ch = 0; ch < channels; ++ch) {
            std::fill(spectrum.begin(), spectrum.end(),
                      std::complex<float>{0.0f, 0.0f});
            for (int i = 0; i < fftSize; ++i) {
                const int64_t index = start + i;
                if (index >= 0 && index < length) {
                    spectrum[static_cast<size_t>(i)] = {
                        audio.sample(ch, selected.start + index) *
                            window[static_cast<size_t>(i)],
                        0.0f};
                }
            }
            fft.forward(spectrum);
            processor(spectrum, ch, frame);
            for (int bin = 1; bin < bins - 1; ++bin) {
                spectrum[static_cast<size_t>(fftSize - bin)] =
                    std::conj(spectrum[static_cast<size_t>(bin)]);
            }
            if (bins > 0) {
                spectrum[0] = {spectrum[0].real(), 0.0f};
                spectrum[static_cast<size_t>(fftSize / 2)] = {
                    spectrum[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
            }
            fft.inverse(spectrum);
            for (int i = 0; i < fftSize; ++i) {
                const int64_t index = start + i;
                if (index >= length) break;
                const double weight = window[static_cast<size_t>(i)];
                processed.sample(ch, index) += static_cast<float>(
                    spectrum[static_cast<size_t>(i)].real() * weight);
                if (ch == 0)
                    windowSum[static_cast<size_t>(index)] += weight * weight;
            }
        }
    }

    for (int64_t i = 0; i < length; ++i) {
        const double weight = windowSum[static_cast<size_t>(i)];
        if (weight > 1e-8) {
            const float scale = static_cast<float>(1.0 / weight);
            for (int ch = 0; ch < channels; ++ch)
                processed.sample(ch, i) *= scale;
        }
    }

    Audio result = audio;
    for (int ch = 0; ch < channels; ++ch) {
        std::copy_n(processed.channel(ch), length,
                    result.channel(ch) + selected.start);
    }
    return result;
}

double binFrequency(int bin, int fftSize, int sampleRate) {
    return static_cast<double>(bin) * sampleRate / fftSize;
}

double bandMask(double frequency, double low, double high, double feather) {
    if (frequency < low - feather || frequency > high + feather) return 0.0;
    if (frequency >= low + feather && frequency <= high - feather) return 1.0;
    if (frequency < low + feather) {
        const double t = std::clamp(
            (frequency - (low - feather)) / (2.0 * feather), 0.0, 1.0);
        return 0.5 - 0.5 * std::cos(kPi * t);
    }
    const double t = std::clamp(
        ((high + feather) - frequency) / (2.0 * feather), 0.0, 1.0);
    return 0.5 - 0.5 * std::cos(kPi * t);
}

} // namespace

float Spectrogram::magnitudeAt(int frame, int bin) const {
    if (frame < 0 || frame >= frameCount || bin < 0 || bin >= binCount)
        return 0.0f;
    return magnitudes[static_cast<size_t>(frame) *
                          static_cast<size_t>(binCount) +
                      static_cast<size_t>(bin)];
}

float Spectrogram::dbAt(int frame, int bin) const {
    if (frame < 0 || frame >= frameCount || bin < 0 || bin >= binCount)
        return -120.0f;
    return decibels[static_cast<size_t>(frame) *
                        static_cast<size_t>(binCount) +
                    static_cast<size_t>(bin)];
}

double Spectrogram::binFrequency(int bin) const {
    if (bin < 0 || bin >= binCount || fftSize <= 0) return 0.0;
    return static_cast<double>(bin) * sampleRate / fftSize;
}

double Spectrogram::frameSeconds(int frame) const {
    if (frame < 0 || sampleRate <= 0) return 0.0;
    return static_cast<double>(frame) * hopSize / sampleRate;
}

Spectrogram computeSpectrogram(const Audio& audio, int requestedFft,
                               int requestedHop, Range range) {
    Spectrogram result;
    Range selected;
    if (!resolveRange(audio, range, selected)) return result;
    const int fftSize = normalizedFftSize(requestedFft);
    const int hop = normalizedHop(requestedHop, fftSize);
    const int bins = fftSize / 2 + 1;
    const int64_t length = selected.length();
    const int frames = static_cast<int>((length + hop - 1) / hop);
    if (frames <= 0) return result;

    result.fftSize = fftSize;
    result.hopSize = hop;
    result.sampleRate = audio.sampleRate();
    result.frameCount = frames;
    result.binCount = bins;
    result.magnitudes.assign(
        static_cast<size_t>(frames) * static_cast<size_t>(bins), 0.0f);
    result.decibels.assign(result.magnitudes.size(), -120.0f);

    const std::vector<float> window = hannWindow(fftSize);
    FFT fft(fftSize);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));
    double maxDb = -120.0;

    for (int frame = 0; frame < frames; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * hop;
        for (int bin = 0; bin < bins; ++bin)
            spectrum[static_cast<size_t>(bin)] = {0.0f, 0.0f};
        for (int i = 0; i < fftSize; ++i) {
            const int64_t index = start + i;
            double value = 0.0;
            if (index >= 0 && index < length) {
                for (int ch = 0; ch < audio.numChannels(); ++ch)
                    value += audio.sample(ch, selected.start + index);
                value /= static_cast<double>(audio.numChannels());
            }
            spectrum[static_cast<size_t>(i)] = {
                static_cast<float>(value) * window[static_cast<size_t>(i)],
                0.0f};
        }
        fft.forward(spectrum);
        for (int bin = 0; bin < bins; ++bin) {
            const float magnitude = std::abs(spectrum[static_cast<size_t>(bin)]);
            const float db = static_cast<float>(
                20.0 * std::log10(static_cast<double>(magnitude) + 1e-12));
            const size_t index = static_cast<size_t>(frame) *
                                     static_cast<size_t>(bins) +
                                 static_cast<size_t>(bin);
            result.magnitudes[index] = magnitude;
            result.decibels[index] = db;
            maxDb = std::max(maxDb, static_cast<double>(db));
        }
    }

    result.minDb = -120.0;
    result.maxDb = std::max(result.minDb, maxDb);
    for (float& db : result.decibels)
        db = static_cast<float>(std::max(static_cast<double>(db), result.minDb));
    return result;
}

float PhaseSpectrogram::dbAt(int frame, int bin) const {
    if (frame < 0 || frame >= frameCount || bin < 0 || bin >= binCount)
        return -120.0f;
    return decibels[static_cast<size_t>(frame) *
                        static_cast<size_t>(binCount) +
                    static_cast<size_t>(bin)];
}

float PhaseSpectrogram::phaseAt(int frame, int bin) const {
    if (frame < 0 || frame >= frameCount || bin < 0 || bin >= binCount)
        return 0.0f;
    return phases[static_cast<size_t>(frame) *
                      static_cast<size_t>(binCount) +
                  static_cast<size_t>(bin)];
}

float PhaseSpectrogram::instantaneousHzAt(int frame, int bin) const {
    if (frame < 0 || frame >= frameCount || bin < 0 || bin >= binCount)
        return 0.0f;
    return instantaneousHz[static_cast<size_t>(frame) *
                               static_cast<size_t>(binCount) +
                           static_cast<size_t>(bin)];
}

float PhaseSpectrogram::confidenceAt(int frame, int bin) const {
    if (frame < 0 || frame >= frameCount || bin < 0 || bin >= binCount)
        return 0.0f;
    return confidence[static_cast<size_t>(frame) *
                          static_cast<size_t>(binCount) +
                      static_cast<size_t>(bin)];
}

double PhaseSpectrogram::binFrequency(int bin) const {
    if (bin < 0 || bin >= binCount || fftSize <= 0) return 0.0;
    return static_cast<double>(bin) * sampleRate / fftSize;
}

double PhaseSpectrogram::frameSeconds(int frame) const {
    if (frame < 0 || sampleRate <= 0) return 0.0;
    return static_cast<double>(frame) * hopSize / sampleRate;
}

PhaseSpectrogram computePhaseSpectrogram(const Audio& audio, int requestedFft,
                                         int maxFrames, Range range) {
    PhaseSpectrogram result;
    Range selected;
    if (!resolveRange(audio, range, selected)) return result;
    const int fftSize = normalizedFftSize(requestedFft);
    const int frameLimit = std::clamp(maxFrames, 16, 8192);
    const int64_t length = selected.length();
    if (length <= 0) return result;
    const int analysisHop = std::max(256, fftSize / 2);
    const int analysisFrames = static_cast<int>(
        (length + analysisHop - 1) / analysisHop);
    if (analysisFrames <= 0) return result;
    const int stride = std::max(
        1, (analysisFrames + frameLimit - 1) / frameLimit);
    const int frames = static_cast<int>(
        (analysisFrames + stride - 1) / stride);
    if (frames <= 0) return result;
    const int bins = fftSize / 2 + 1;

    result.fftSize = fftSize;
    result.hopSize = analysisHop * stride;
    result.sampleRate = audio.sampleRate();
    result.frameCount = frames;
    result.binCount = bins;
    const size_t total =
        static_cast<size_t>(frames) * static_cast<size_t>(bins);
    result.decibels.assign(total, -120.0f);
    result.phases.assign(total, 0.0f);
    result.instantaneousHz.assign(total, 0.0f);
    result.confidence.assign(total, 0.0f);

    const std::vector<float> window = hannWindow(fftSize);
    FFT fft(fftSize);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));
    std::vector<float> previousPhase(static_cast<size_t>(bins), 0.0f);
    std::vector<bool> previousValid(static_cast<size_t>(bins), false);
    const double sampleRate = static_cast<double>(audio.sampleRate());
    const double twoPi = 2.0 * kPi;
    double maxDb = -120.0;

    for (int analysisFrame = 0; analysisFrame < analysisFrames;
         ++analysisFrame) {
        const int64_t start =
            static_cast<int64_t>(analysisFrame) * analysisHop;
        std::fill(spectrum.begin(), spectrum.end(),
                  std::complex<float>{0.0f, 0.0f});
        for (int i = 0; i < fftSize; ++i) {
            const int64_t index = start + i;
            double value = 0.0;
            if (index >= 0 && index < length) {
                for (int ch = 0; ch < audio.numChannels(); ++ch)
                    value += audio.sample(ch, selected.start + index);
                value /= static_cast<double>(audio.numChannels());
            }
            spectrum[static_cast<size_t>(i)] = {
                static_cast<float>(value * window[static_cast<size_t>(i)]),
                0.0f};
        }
        fft.forward(spectrum);
        const bool store = (analysisFrame % stride) == 0;
        const int frame = analysisFrame / stride;
        for (int bin = 0; bin < bins; ++bin) {
            const std::complex<float>& value =
                spectrum[static_cast<size_t>(bin)];
            const float magnitude = std::abs(value);
            const float db = static_cast<float>(
                20.0 * std::log10(static_cast<double>(magnitude) + 1e-12));
            const float phase = std::arg(value);
            float instantaneous = 0.0f;
            float confidence = 0.0f;
            const float magnitudeConfidence = std::clamp(
                static_cast<float>((db + 90.0) / 60.0), 0.0f, 1.0f);
            const double binHz =
                static_cast<double>(bin) * sampleRate / fftSize;
            if (previousValid[static_cast<size_t>(bin)]) {
                const double expected =
                    twoPi * static_cast<double>(bin) *
                    static_cast<double>(analysisHop) /
                    static_cast<double>(fftSize);
                const double delta =
                    static_cast<double>(phase) -
                    static_cast<double>(previousPhase[static_cast<size_t>(bin)]);
                const double deviation =
                    std::remainder(delta - expected, twoPi);
                const double instantaneousBin =
                    static_cast<double>(bin) +
                    deviation * static_cast<double>(fftSize) /
                        (twoPi * static_cast<double>(analysisHop));
                instantaneous = static_cast<float>(std::clamp(
                    instantaneousBin * sampleRate / fftSize, 0.0,
                    sampleRate / 2.0));
                const float stability = static_cast<float>(
                    1.0 - std::min(1.0, std::abs(deviation) / kPi));
                confidence =
                    magnitudeConfidence * (0.25f + 0.75f * stability);
            } else {
                instantaneous = static_cast<float>(binHz);
                confidence = magnitudeConfidence * 0.75f;
            }
            if (store) {
                const size_t index = static_cast<size_t>(frame) *
                                         static_cast<size_t>(bins) +
                                     static_cast<size_t>(bin);
                result.decibels[index] = db;
                result.phases[index] = phase;
                result.instantaneousHz[index] = instantaneous;
                result.confidence[index] = confidence;
                maxDb = std::max(maxDb, static_cast<double>(db));
            }
            previousPhase[static_cast<size_t>(bin)] = phase;
            previousValid[static_cast<size_t>(bin)] = true;
        }
    }

    result.minDb = -120.0;
    result.maxDb = std::max(result.minDb, maxDb);
    for (float& db : result.decibels)
        db = static_cast<float>(std::max(static_cast<double>(db), result.minDb));
    return result;
}

Audio spectralGain(const Audio& audio, double lowHz, double highHz,
                   double gainDb, Range range, int fftSize, int hopSize) {
    if (!std::isfinite(lowHz) || !std::isfinite(highHz) ||
        !std::isfinite(gainDb) || lowHz < 0.0 || highHz <= lowHz)
        return Audio{};
    const int fft = normalizedFftSize(fftSize);
    const int hop = normalizedHop(hopSize, fft);
    const int bins = fft / 2 + 1;
    const double feather = std::max(
        static_cast<double>(audio.sampleRate()) / fft,
        (highHz - lowHz) * 0.05);
    const double gain = std::pow(10.0, gainDb / 20.0);
    auto processor = [=](std::vector<std::complex<float>>& spectrum, int, int) {
        for (int bin = 0; bin < bins; ++bin) {
            const double frequency =
                binFrequency(bin, fft, audio.sampleRate());
            const double mask = bandMask(frequency, lowHz, highHz, feather);
            const float scale =
                static_cast<float>(1.0 + (gain - 1.0) * mask);
            spectrum[static_cast<size_t>(bin)] *= scale;
        }
    };
    return processSpectralRange(audio, range, fft, hop, processor);
}

Audio spectralFilter(const Audio& audio, const std::string& type,
                     double cutoffHz, Range range, int order, int fftSize,
                     int hopSize) {
    if (!std::isfinite(cutoffHz) || cutoffHz <= 0.0) return Audio{};
    if (type != "lowpass" && type != "highpass") return Audio{};
    const int fft = normalizedFftSize(fftSize);
    const int hop = normalizedHop(hopSize, fft);
    const int bins = fft / 2 + 1;
    const int filterOrder = std::clamp(order, 1, 12);
    auto processor = [=](std::vector<std::complex<float>>& spectrum, int, int) {
        for (int bin = 0; bin < bins; ++bin) {
            const double frequency =
                binFrequency(bin, fft, audio.sampleRate());
            double response = 0.0;
            if (type == "lowpass") {
                if (frequency <= 0.0) response = 1.0;
                else {
                    const double ratio = frequency / cutoffHz;
                    response = 1.0 / std::sqrt(
                        1.0 + std::pow(ratio, 2.0 * filterOrder));
                }
            } else {
                if (frequency <= 0.0) response = 0.0;
                else {
                    const double ratio = frequency / cutoffHz;
                    response = std::pow(ratio, filterOrder) / std::sqrt(
                        1.0 + std::pow(ratio, 2.0 * filterOrder));
                }
            }
            spectrum[static_cast<size_t>(bin)] *= static_cast<float>(response);
        }
    };
    return processSpectralRange(audio, range, fft, hop, processor);
}

Audio spectralGate(const Audio& audio, double thresholdDb, Range range,
                   int fftSize, int hopSize) {
    if (!std::isfinite(thresholdDb)) return Audio{};
    const int fft = normalizedFftSize(fftSize);
    const int hop = normalizedHop(hopSize, fft);
    const int bins = fft / 2 + 1;
    const double threshold = std::pow(10.0, thresholdDb / 20.0);
    auto processor = [=](std::vector<std::complex<float>>& spectrum, int, int) {
        float maximum = 0.0f;
        for (int bin = 0; bin < bins; ++bin)
            maximum = std::max(maximum,
                               std::abs(spectrum[static_cast<size_t>(bin)]));
        if (maximum <= 0.0f) return;
        const float cutoff = static_cast<float>(maximum * threshold);
        for (int bin = 0; bin < bins; ++bin) {
            if (std::abs(spectrum[static_cast<size_t>(bin)]) < cutoff)
                spectrum[static_cast<size_t>(bin)] = {0.0f, 0.0f};
        }
    };
    return processSpectralRange(audio, range, fft, hop, processor);
}

Audio spectralBlur(const Audio& audio, int timeRadius, int frequencyRadius,
                   Range range, int fftSize, int hopSize) {
    if (timeRadius < 0 || frequencyRadius < 0) return Audio{};
    const int fft = normalizedFftSize(fftSize);
    const int hop = normalizedHop(hopSize, fft);
    const int bins = fft / 2 + 1;
    const int freqRadius = std::clamp(frequencyRadius, 0, bins - 1);
    const double timeCoefficient =
        timeRadius <= 0 ? 1.0
                        : std::clamp(1.0 / (1.0 + static_cast<double>(timeRadius)),
                                     0.02, 1.0);
    const int channels = audio.numChannels();
    std::vector<std::vector<float>> smooth(static_cast<size_t>(channels),
                                           std::vector<float>(
                                               static_cast<size_t>(bins), 0.0f));
    std::vector<std::vector<char>> initialized(static_cast<size_t>(channels),
                                               std::vector<char>(1, 0));
    std::vector<float> blurred(static_cast<size_t>(bins), 0.0f);

    auto processor = [&](std::vector<std::complex<float>>& spectrum, int ch,
                         int) {
        auto& smoothed = smooth[static_cast<size_t>(ch)];
        auto& hasPrevious = initialized[static_cast<size_t>(ch)][0];
        for (int bin = 0; bin < bins; ++bin) {
            const float magnitude =
                std::abs(spectrum[static_cast<size_t>(bin)]);
            smoothed[static_cast<size_t>(bin)] =
                hasPrevious
                    ? static_cast<float>(
                          (1.0 - timeCoefficient) *
                              smoothed[static_cast<size_t>(bin)] +
                          timeCoefficient * magnitude)
                    : magnitude;
        }
        hasPrevious = 1;
        if (freqRadius > 0) {
            for (int bin = 0; bin < bins; ++bin) {
                double sum = 0.0;
                int count = 0;
                const int lo = std::max(0, bin - freqRadius);
                const int hi = std::min(bins - 1, bin + freqRadius);
                for (int j = lo; j <= hi; ++j) {
                    sum += smoothed[static_cast<size_t>(j)];
                    ++count;
                }
                blurred[static_cast<size_t>(bin)] =
                    static_cast<float>(sum / std::max(1, count));
            }
        } else {
            blurred = smoothed;
        }
        for (int bin = 0; bin < bins; ++bin) {
            const auto& value = spectrum[static_cast<size_t>(bin)];
            const float phase = std::atan2(value.imag(), value.real());
            const float newMagnitude = blurred[static_cast<size_t>(bin)];
            spectrum[static_cast<size_t>(bin)] = {
                newMagnitude * std::cos(phase),
                newMagnitude * std::sin(phase)};
        }
    };
    return processSpectralRange(audio, range, fft, hop, processor);
}

} // namespace vwave
