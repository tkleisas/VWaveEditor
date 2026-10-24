#include "vwave/Processing.h"

#include "vwave/FFT.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

float readSample(const Audio& audio, int channel, int64_t frame) {
    if (channel < 0 || channel >= audio.numChannels() || frame < 0 ||
        frame >= audio.numFrames())
        return 0.0f;
    return audio.sample(channel, frame);
}

int64_t scaledLength(int64_t frames, double ratio) {
    if (frames <= 0 || !std::isfinite(ratio) || ratio <= 0.0) return 0;
    const double value = static_cast<double>(frames) * ratio;
    if (value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
        return 0;
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(value)));
}

double sinc(double value) {
    if (std::abs(value) < 1e-12) return 1.0;
    const double angle = kPi * value;
    return std::sin(angle) / angle;
}

Audio resampleInternal(const Audio& audio, int64_t outputFrames,
                       double sourceStep, int outputSampleRate,
                       const ResampleOptions& options) {
    if (audio.empty() || outputFrames <= 0 || !std::isfinite(sourceStep) ||
        sourceStep <= 0.0)
        return Audio{};

    const int channels = audio.numChannels();
    Audio out(channels, outputFrames, outputSampleRate);
    const int halfWidth = std::clamp(options.zeroCrossings, 1, 128);
    const double cutoff =
        std::clamp(options.rolloff, 0.01, 1.0) * std::min(1.0, 1.0 / sourceStep);
    const int64_t sourceFrames = audio.numFrames();

    for (int64_t frame = 0; frame < outputFrames; ++frame) {
        const double center =
            (static_cast<double>(frame) + 0.5) * sourceStep - 0.5;
        const int64_t first = static_cast<int64_t>(std::floor(center)) - halfWidth + 1;
        const int64_t last = static_cast<int64_t>(std::floor(center)) + halfWidth;
        std::vector<float> weights;
        weights.reserve(static_cast<size_t>(2 * halfWidth));
        double weightSum = 0.0;

        for (int64_t index = first; index <= last; ++index) {
            const double distance = center - static_cast<double>(index);
            if (std::abs(distance) > static_cast<double>(halfWidth)) continue;
            const double window = 0.5 + 0.5 * std::cos(
                kPi * distance / static_cast<double>(halfWidth));
            const double weight =
                cutoff * sinc(cutoff * distance) * window;
            weights.push_back(static_cast<float>(weight));
            weightSum += weight;
        }

        if (std::abs(weightSum) < 1e-12) continue;
        const double normalization = 1.0 / weightSum;
        for (int ch = 0; ch < channels; ++ch) {
            double sum = 0.0;
            int64_t index = first;
            for (float weight : weights) {
                if (index >= 0 && index < sourceFrames)
                    sum += static_cast<double>(audio.sample(ch, index)) * weight;
                ++index;
            }
            out.sample(ch, frame) = static_cast<float>(sum * normalization);
        }
    }
    return out;
}

Audio wsolaStretch(const Audio& input, int64_t outputFrames, double ratio,
                   const StretchOptions& options) {
    const int channels = input.numChannels();
    const int64_t inputFrames = input.numFrames();
    int frameSize = std::max(64, options.wsolaFrameSize);
    if ((frameSize & 1) != 0) ++frameSize;
    const int hop = frameSize / 2;
    const int search = std::clamp(options.wsolaSearchRadius, 0,
                                  std::max(0, frameSize / 2));
    const double analysisStep = static_cast<double>(hop) / ratio;

    Audio output(channels, outputFrames, input.sampleRate());
    std::vector<double> windowSum(static_cast<size_t>(outputFrames), 0.0);
    std::vector<float> window(static_cast<size_t>(frameSize));
    for (int i = 0; i < frameSize; ++i) {
        window[static_cast<size_t>(i)] = static_cast<float>(
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                 static_cast<double>(frameSize)));
    }

    int64_t previousStart = 0;
    const int64_t frameStarts = (outputFrames + hop - 1) / hop;
    for (int64_t frameIndex = 0; frameIndex < frameStarts; ++frameIndex) {
        const int64_t outputStart = frameIndex * hop;
        if (outputStart >= outputFrames) break;
        const int64_t nominal = static_cast<int64_t>(
            std::llround(static_cast<double>(frameIndex) * analysisStep));
        int64_t bestStart = std::clamp<int64_t>(nominal, 0,
                                                std::max<int64_t>(0, inputFrames - 1));

        if (frameIndex > 0 && search > 0 && inputFrames > 0) {
            double bestScore = -std::numeric_limits<double>::infinity();
            const int compareLength = hop;
            for (int offset = -search; offset <= search; ++offset) {
                const int64_t candidate = std::clamp<int64_t>(
                    nominal + offset, 0, std::max<int64_t>(0, inputFrames - 1));
                double correlation = 0.0;
                double normA = 0.0;
                double normB = 0.0;
                for (int i = 0; i < compareLength; ++i) {
                    const double a = readSample(input, 0,
                                                previousStart + hop + i);
                    double b = 0.0;
                    for (int ch = 0; ch < channels; ++ch)
                        b += readSample(input, ch, candidate + i);
                    b /= static_cast<double>(channels);
                    correlation += a * b;
                    normA += a * a;
                    normB += b * b;
                }
                const double denominator = std::sqrt(normA * normB);
                const double score = denominator > 1e-12
                                         ? correlation / denominator
                                         : 0.0;
                if (score > bestScore) {
                    bestScore = score;
                    bestStart = candidate;
                }
            }
        }

        for (int i = 0; i < frameSize; ++i) {
            const int64_t outIndex = outputStart + i;
            if (outIndex >= outputFrames) break;
            const double weight = window[static_cast<size_t>(i)];
            for (int ch = 0; ch < channels; ++ch) {
                output.sample(ch, outIndex) += static_cast<float>(
                    readSample(input, ch, bestStart + i) * weight);
            }
            windowSum[static_cast<size_t>(outIndex)] += weight;
        }
        previousStart = bestStart;
    }

    for (int64_t frame = 0; frame < outputFrames; ++frame) {
        const double weight = windowSum[static_cast<size_t>(frame)];
        if (weight > 1e-8) {
            const float scale = static_cast<float>(1.0 / weight);
            for (int ch = 0; ch < channels; ++ch)
                output.sample(ch, frame) *= scale;
        }
    }
    return output;
}

double wrapPhase(double phase) {
    return phase - 2.0 * kPi * std::round(phase / (2.0 * kPi));
}

Audio phaseVocoderStretch(const Audio& input, int64_t outputFrames,
                          double ratio, const StretchOptions& options) {
    const int channels = input.numChannels();
    const int64_t inputFrames = input.numFrames();
    const int fftSize = nextPowerOfTwo(std::max(256, options.fftSize));
    const int hop = std::clamp(options.hopSize, 16, fftSize / 2);
    const double analysisHop = static_cast<double>(hop) / ratio;
    const int bins = fftSize / 2 + 1;
    const std::vector<float> window = hannWindow(fftSize);
    FFT fft(fftSize);

    Audio output(channels, outputFrames, input.sampleRate());
    std::vector<double> windowSum(static_cast<size_t>(outputFrames), 0.0);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));

    std::vector<std::vector<double>> previousPhase(
        static_cast<size_t>(channels),
        std::vector<double>(static_cast<size_t>(bins), 0.0));
    std::vector<std::vector<double>> sumPhase(
        static_cast<size_t>(channels),
        std::vector<double>(static_cast<size_t>(bins), 0.0));
    std::vector<char> firstFrame(static_cast<size_t>(channels), 1);

    const int64_t frameCount = (outputFrames + hop - 1) / hop;
    for (int64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int64_t outputStart = frameIndex * hop;
        if (outputStart >= outputFrames) break;
        const int64_t inputStart = static_cast<int64_t>(
            std::llround(static_cast<double>(frameIndex) * analysisHop));

        for (int ch = 0; ch < channels; ++ch) {
            std::fill(spectrum.begin(), spectrum.end(),
                      std::complex<float>{0.0f, 0.0f});
            for (int i = 0; i < fftSize; ++i) {
                const int64_t source = inputStart + i;
                if (source >= 0 && source < inputFrames) {
                    spectrum[static_cast<size_t>(i)] = {
                        readSample(input, ch, source) *
                            window[static_cast<size_t>(i)],
                        0.0f};
                }
            }
            fft.forward(spectrum);

            for (int bin = 0; bin < bins; ++bin) {
                const auto& value = spectrum[static_cast<size_t>(bin)];
                const double magnitude = std::abs(value);
                const double phase = std::atan2(value.imag(), value.real());
                const double omega = 2.0 * kPi * static_cast<double>(bin) /
                                     static_cast<double>(fftSize);
                double synthesisPhase = phase;
                if (firstFrame[static_cast<size_t>(ch)]) {
                    sumPhase[static_cast<size_t>(ch)][static_cast<size_t>(bin)] =
                        phase;
                } else {
                    const double expected = omega * analysisHop;
                    const double delta = wrapPhase(
                        phase -
                        previousPhase[static_cast<size_t>(ch)][static_cast<size_t>(bin)] -
                        expected);
                    const double instantaneous =
                        omega + delta / analysisHop;
                    auto& accumulated =
                        sumPhase[static_cast<size_t>(ch)][static_cast<size_t>(bin)];
                    accumulated += instantaneous * static_cast<double>(hop);
                    synthesisPhase = accumulated;
                }
                previousPhase[static_cast<size_t>(ch)][static_cast<size_t>(bin)] =
                    phase;
                spectrum[static_cast<size_t>(bin)] = {
                    static_cast<float>(magnitude * std::cos(synthesisPhase)),
                    static_cast<float>(magnitude * std::sin(synthesisPhase))};
                if (bin > 0 && bin < fftSize / 2) {
                    spectrum[static_cast<size_t>(fftSize - bin)] =
                        std::conj(spectrum[static_cast<size_t>(bin)]);
                }
            }
            if (fftSize > 1) {
                spectrum[0] = {spectrum[0].real(), 0.0f};
                spectrum[static_cast<size_t>(fftSize / 2)] = {
                    spectrum[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
            }

            fft.inverse(spectrum);
            for (int i = 0; i < fftSize; ++i) {
                const int64_t outIndex = outputStart + i;
                if (outIndex >= outputFrames) break;
                const double weight = window[static_cast<size_t>(i)];
                output.sample(ch, outIndex) += static_cast<float>(
                    spectrum[static_cast<size_t>(i)].real() * weight);
                if (ch == 0)
                    windowSum[static_cast<size_t>(outIndex)] += weight * weight;
            }
            firstFrame[static_cast<size_t>(ch)] = 0;
        }
    }

    for (int64_t frame = 0; frame < outputFrames; ++frame) {
        const double weight = windowSum[static_cast<size_t>(frame)];
        if (weight > 1e-8) {
            const float scale = static_cast<float>(1.0 / weight);
            for (int ch = 0; ch < channels; ++ch)
                output.sample(ch, frame) *= scale;
        }
    }
    return output;
}

uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

Audio paulStretchInternal(const Audio& input, int64_t outputFrames,
                          double ratio, const StretchOptions& options) {
    const int channels = input.numChannels();
    const int64_t inputFrames = input.numFrames();
    const int fftSize = nextPowerOfTwo(std::max(256, options.fftSize));
    const int hop = std::clamp(options.hopSize, 16, fftSize / 2);
    const double analysisHop = static_cast<double>(hop) / ratio;
    const int bins = fftSize / 2 + 1;
    const std::vector<float> window = hannWindow(fftSize);
    FFT fft(fftSize);

    Audio output(channels, outputFrames, input.sampleRate());
    std::vector<double> windowSum(static_cast<size_t>(outputFrames), 0.0);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));

    std::vector<std::vector<double>> previousPhase(
        static_cast<size_t>(channels),
        std::vector<double>(static_cast<size_t>(bins), 0.0));
    std::vector<std::vector<double>> sumPhase(
        static_cast<size_t>(channels),
        std::vector<double>(static_cast<size_t>(bins), 0.0));
    std::vector<char> firstFrame(static_cast<size_t>(channels), 1);

    const int64_t frameCount = (outputFrames + hop - 1) / hop;
    for (int64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int64_t outputStart = frameIndex * hop;
        if (outputStart >= outputFrames) break;
        const int64_t inputStart = static_cast<int64_t>(
            std::llround(static_cast<double>(frameIndex) * analysisHop));

        for (int ch = 0; ch < channels; ++ch) {
            uint32_t randomState =
                0x9e3779b9u ^ static_cast<uint32_t>(ch * 7919) ^
                static_cast<uint32_t>(frameIndex * 104729);

            std::fill(spectrum.begin(), spectrum.end(),
                      std::complex<float>{0.0f, 0.0f});
            for (int i = 0; i < fftSize; ++i) {
                const int64_t source = inputStart + i;
                if (source >= 0 && source < inputFrames) {
                    spectrum[static_cast<size_t>(i)] = {
                        readSample(input, ch, source) *
                            window[static_cast<size_t>(i)],
                        0.0f};
                }
            }
            fft.forward(spectrum);

            for (int bin = 0; bin < bins; ++bin) {
                const auto& value = spectrum[static_cast<size_t>(bin)];
                const double magnitude = std::abs(value);
                const double phase = std::atan2(value.imag(), value.real());
                const double omega = 2.0 * kPi * static_cast<double>(bin) /
                                     static_cast<double>(fftSize);
                auto& accumulated =
                    sumPhase[static_cast<size_t>(ch)][static_cast<size_t>(bin)];
                auto& previous =
                    previousPhase[static_cast<size_t>(ch)][static_cast<size_t>(bin)];

                if (firstFrame[static_cast<size_t>(ch)]) {
                    accumulated =
                        (static_cast<double>(nextRandom(randomState)) /
                             static_cast<double>(std::numeric_limits<uint32_t>::max()) *
                             2.0 -
                         1.0) *
                        kPi;
                } else {
                    const double expected = omega * analysisHop;
                    const double delta = wrapPhase(phase - previous - expected);
                    const double instantaneous = omega + delta / analysisHop;
                    accumulated += instantaneous * static_cast<double>(hop);
                }
                previous = phase;

                const double synthesisPhase = accumulated;
                spectrum[static_cast<size_t>(bin)] = {
                    static_cast<float>(magnitude * std::cos(synthesisPhase)),
                    static_cast<float>(magnitude * std::sin(synthesisPhase))};
                if (bin > 0 && bin < fftSize / 2) {
                    spectrum[static_cast<size_t>(fftSize - bin)] =
                        std::conj(spectrum[static_cast<size_t>(bin)]);
                }
            }
            if (fftSize > 1) {
                spectrum[0] = {spectrum[0].real(), 0.0f};
                spectrum[static_cast<size_t>(fftSize / 2)] = {
                    spectrum[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
            }

            fft.inverse(spectrum);
            for (int i = 0; i < fftSize; ++i) {
                const int64_t outIndex = outputStart + i;
                if (outIndex >= outputFrames) break;
                const double weight = window[static_cast<size_t>(i)];
                output.sample(ch, outIndex) += static_cast<float>(
                    spectrum[static_cast<size_t>(i)].real() * weight);
                if (ch == 0)
                    windowSum[static_cast<size_t>(outIndex)] += weight * weight;
            }
            firstFrame[static_cast<size_t>(ch)] = 0;
        }
    }

    for (int64_t frame = 0; frame < outputFrames; ++frame) {
        const double weight = windowSum[static_cast<size_t>(frame)];
        if (weight > 1e-8) {
            const float scale = static_cast<float>(1.0 / weight);
            for (int ch = 0; ch < channels; ++ch)
                output.sample(ch, frame) *= scale;
        }
    }
    return output;
}


Audio fitFrameCount(Audio audio, int64_t frames) {
    if (audio.numFrames() == frames) return audio;
    Audio out(audio.numChannels(), frames, audio.sampleRate());
    const int64_t copyFrames = std::min(frames, audio.numFrames());
    for (int ch = 0; ch < audio.numChannels(); ++ch)
        std::copy_n(audio.channel(ch), copyFrames, out.channel(ch));
    return out;
}

} // namespace

Audio resample(const Audio& audio, int targetSampleRate,
               const ResampleOptions& options) {
    if (audio.empty() || targetSampleRate <= 0) return Audio{};
    if (targetSampleRate == audio.sampleRate()) return audio;
    const double ratio = static_cast<double>(targetSampleRate) /
                         static_cast<double>(audio.sampleRate());
    const int64_t outFrames = scaledLength(audio.numFrames(), ratio);
    return resampleInternal(audio, outFrames, 1.0 / ratio, targetSampleRate,
                            options);
}

Audio resampleByRatio(const Audio& audio, double ratio,
                      const ResampleOptions& options) {
    if (audio.empty() || !std::isfinite(ratio) || ratio <= 0.0) return Audio{};
    const int64_t outFrames = scaledLength(audio.numFrames(), ratio);
    return resampleInternal(audio, outFrames, 1.0 / ratio, audio.sampleRate(),
                            options);
}

Audio timeStretch(const Audio& audio, double ratio, StretchAlgorithm algorithm,
                  const StretchOptions& options) {
    if (audio.empty() || !std::isfinite(ratio) || ratio <= 0.0) return Audio{};
    const int64_t outFrames = scaledLength(audio.numFrames(), ratio);
    if (outFrames <= 0) return Audio{};
    if (std::abs(ratio - 1.0) < 1e-12) return audio;
    switch (algorithm) {
    case StretchAlgorithm::Wsola:
        return wsolaStretch(audio, outFrames, ratio, options);
    case StretchAlgorithm::PhaseVocoder:
        return phaseVocoderStretch(audio, outFrames, ratio, options);
    case StretchAlgorithm::PaulStretch:
        return paulStretchInternal(audio, outFrames, ratio, options);
    }
    return Audio{};
}

Audio pitchShift(const Audio& audio, double semitones,
                 StretchAlgorithm algorithm, const StretchOptions& options) {
    if (audio.empty() || !std::isfinite(semitones)) return Audio{};
    if (std::abs(semitones) < 1e-12) return audio;
    const double ratio = std::pow(2.0, semitones / 12.0);
    if (!std::isfinite(ratio) || ratio <= 0.0) return Audio{};
    Audio stretched = timeStretch(audio, ratio, algorithm, options);
    if (stretched.empty()) return Audio{};
    const double correction =
        static_cast<double>(audio.numFrames()) /
        static_cast<double>(stretched.numFrames());
    Audio shifted = resampleByRatio(stretched, correction);
    return fitFrameCount(std::move(shifted), audio.numFrames());
}

Audio paulStretch(const Audio& audio, double ratio,
                  const StretchOptions& options) {
    if (audio.empty() || !std::isfinite(ratio) || ratio <= 0.0) return Audio{};
    const int64_t outFrames = scaledLength(audio.numFrames(), ratio);
    return paulStretchInternal(audio, outFrames, ratio, options);
}

Audio convolve(const Audio& audio, const Audio& impulseResponse,
               bool normalizeIr) {
    if (audio.empty() || impulseResponse.empty()) return Audio{};
    if (audio.numChannels() != impulseResponse.numChannels() &&
        impulseResponse.numChannels() != 1)
        return Audio{};

    Audio ir = impulseResponse;
    if (ir.sampleRate() != audio.sampleRate())
        ir = resample(ir, audio.sampleRate());
    if (normalizeIr) {
        const float peak = ir.peak();
        if (peak > 0.0f) {
            const float gain = 1.0f / peak;
            for (int ch = 0; ch < ir.numChannels(); ++ch) {
                float* data = ir.channel(ch);
                for (int64_t i = 0; i < ir.numFrames(); ++i) data[i] *= gain;
            }
        }
    }

    const int64_t outputFrames = audio.numFrames() + ir.numFrames() - 1;
    const int fftSize = nextPowerOfTwo(static_cast<int>(outputFrames));
    FFT fft(fftSize);
    Audio output(audio.numChannels(), outputFrames, audio.sampleRate());
    std::vector<std::complex<float>> a(static_cast<size_t>(fftSize));
    std::vector<std::complex<float>> b(static_cast<size_t>(fftSize));

    for (int ch = 0; ch < audio.numChannels(); ++ch) {
        std::fill(a.begin(), a.end(), std::complex<float>{0.0f, 0.0f});
        std::fill(b.begin(), b.end(), std::complex<float>{0.0f, 0.0f});
        const int irChannel = ir.numChannels() == 1 ? 0 : ch;
        for (int64_t i = 0; i < audio.numFrames(); ++i)
            a[static_cast<size_t>(i)] = audio.sample(ch, i);
        for (int64_t i = 0; i < ir.numFrames(); ++i)
            b[static_cast<size_t>(i)] = ir.sample(irChannel, i);
        fft.forward(a);
        fft.forward(b);
        for (int i = 0; i < fftSize; ++i)
            a[static_cast<size_t>(i)] *= b[static_cast<size_t>(i)];
        fft.inverse(a);
        for (int64_t i = 0; i < outputFrames; ++i)
            output.sample(ch, i) = a[static_cast<size_t>(i)].real();
    }
    return output;
}

Audio applyGain(const Audio& audio, double db) {
    Audio result = audio;
    if (!std::isfinite(db)) return result;
    const float gain = static_cast<float>(std::pow(10.0, db / 20.0));
    for (int ch = 0; ch < result.numChannels(); ++ch) {
        float* data = result.channel(ch);
        for (int64_t i = 0; i < result.numFrames(); ++i) data[i] *= gain;
    }
    return result;
}

Audio normalize(const Audio& audio, double peakDb) {
    Audio result = audio;
    const float peak = result.peak();
    if (peak <= 0.0f || !std::isfinite(peakDb)) return result;
    const double target = std::pow(10.0, peakDb / 20.0);
    const float gain = static_cast<float>(target / static_cast<double>(peak));
    for (int ch = 0; ch < result.numChannels(); ++ch) {
        float* data = result.channel(ch);
        for (int64_t i = 0; i < result.numFrames(); ++i) data[i] *= gain;
    }
    return result;
}

} // namespace vwave
