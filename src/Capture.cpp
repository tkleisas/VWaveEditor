#include "vwave/Capture.h"

#include "vwave/FFT.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <limits>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

float channelSample(const Audio& audio, int channel, int64_t frame) {
    if (channel < 0 || channel >= audio.numChannels() || frame < 0 ||
        frame >= audio.numFrames())
        return 0.0f;
    return audio.sample(channel, frame);
}

std::vector<float> monoReference(const Audio& audio) {
    std::vector<float> mono(static_cast<size_t>(audio.numFrames()), 0.0f);
    for (int64_t frame = 0; frame < audio.numFrames(); ++frame) {
        double value = 0.0;
        for (int ch = 0; ch < audio.numChannels(); ++ch)
            value += audio.sample(ch, frame);
        mono[static_cast<size_t>(frame)] = static_cast<float>(
            value / static_cast<double>(audio.numChannels()));
    }
    return mono;
}

} // namespace

Audio generateSweep(const SweepOptions& options) {
    if (!std::isfinite(options.startHz) || !std::isfinite(options.endHz) ||
        !std::isfinite(options.seconds) || !std::isfinite(options.amplitude))
        return Audio{};
    if (options.startHz <= 0.0 || options.endHz <= options.startHz ||
        options.seconds <= 0.0 || options.amplitude <= 0.0 ||
        options.sampleRate <= 0 || options.channels <= 0)
        return Audio{};

    const int64_t frames = static_cast<int64_t>(
        std::llround(options.seconds * options.sampleRate));
    if (frames <= 0) return Audio{};
    Audio sweep(options.channels, frames, options.sampleRate);
    const double ratio = options.endHz / options.startHz;
    const double logRatio = std::log(ratio);
    const double coefficient =
        2.0 * kPi * options.startHz * options.seconds / logRatio;
    const int64_t fadeFrames = std::min<int64_t>(
        static_cast<int64_t>(0.005 * options.sampleRate), frames / 2);
    for (int64_t frame = 0; frame < frames; ++frame) {
        const double time =
            static_cast<double>(frame) / options.sampleRate;
        const double phase =
            coefficient * (std::exp(logRatio * time / options.seconds) - 1.0);
        double value = options.amplitude * std::sin(phase);
        if (fadeFrames > 0) {
            double fade = 1.0;
            if (frame < fadeFrames) {
                const double position =
                    static_cast<double>(frame) / fadeFrames;
                fade = std::sin(position * kPi / 2.0);
                fade *= fade;
            }
            if (frame >= frames - fadeFrames) {
                const double position =
                    static_cast<double>(frames - 1 - frame) / fadeFrames;
                fade = std::sin(std::clamp(position, 0.0, 1.0) * kPi / 2.0);
                fade *= fade;
            }
            value *= fade;
        }
        for (int ch = 0; ch < options.channels; ++ch)
            sweep.sample(ch, frame) = static_cast<float>(value);
    }
    return sweep;
}

Audio extractImpulseResponse(const Audio& recorded, const Audio& sweep,
                             const DeconvolutionOptions& options,
                             std::string* error) {
    if (error) error->clear();
    if (recorded.empty() || sweep.empty()) {
        setError(error, "recorded audio and sweep must not be empty");
        return Audio{};
    }
    if (recorded.sampleRate() != sweep.sampleRate()) {
        setError(error, "recorded audio and sweep sample rates differ");
        return Audio{};
    }
    if (!std::isfinite(options.preDelaySeconds) ||
        options.preDelaySeconds < 0.0 || options.irLength < 0) {
        setError(error, "invalid deconvolution options");
        return Audio{};
    }

    const int64_t fftLength = nextPowerOfTwo(static_cast<int>(
        std::max<int64_t>(recorded.numFrames(), sweep.numFrames())));
    if (fftLength <= 0) {
        setError(error, "cannot size deconvolution transform");
        return Audio{};
    }
    FFT fft(static_cast<int>(fftLength));
    const std::vector<float> sweepMono = monoReference(sweep);
    std::vector<std::complex<float>> sweepSpectrum(
        static_cast<size_t>(fftLength));
    for (size_t i = 0; i < sweepMono.size(); ++i)
        sweepSpectrum[i] = sweepMono[i];
    fft.forward(sweepSpectrum);

    double powerSum = 0.0;
    for (const auto& value : sweepSpectrum)
        powerSum += static_cast<double>(std::abs(value)) *
                    static_cast<double>(std::abs(value));
    const double regularization =
        std::max(1e-12, powerSum / fftLength * 1e-8);

    int64_t irLength = options.irLength;
    if (irLength <= 0) {
        irLength = std::max<int64_t>(
            1, recorded.numFrames() - sweep.numFrames() + 1);
        irLength = std::min<int64_t>(irLength, fftLength);
    }

    Audio result(recorded.numChannels(), irLength, recorded.sampleRate());
    std::vector<std::complex<float>> recordedSpectrum(
        static_cast<size_t>(fftLength));
    std::vector<std::complex<float>> response(
        static_cast<size_t>(fftLength));
    std::vector<float> impulse(static_cast<size_t>(fftLength), 0.0f);

    for (int ch = 0; ch < recorded.numChannels(); ++ch) {
        std::fill(recordedSpectrum.begin(), recordedSpectrum.end(),
                  std::complex<float>{0.0f, 0.0f});
        for (int64_t i = 0; i < recorded.numFrames(); ++i)
            recordedSpectrum[static_cast<size_t>(i)] =
                channelSample(recorded, ch, i);
        fft.forward(recordedSpectrum);
        for (int64_t bin = 0; bin < fftLength; ++bin) {
            const auto s = sweepSpectrum[static_cast<size_t>(bin)];
            const auto r = recordedSpectrum[static_cast<size_t>(bin)];
            const double denominator =
                static_cast<double>(std::norm(s)) + regularization;
            response[static_cast<size_t>(bin)] = {
                static_cast<float>((r.real() * s.real() +
                                    r.imag() * s.imag()) /
                                   denominator),
                static_cast<float>((r.imag() * s.real() -
                                    r.real() * s.imag()) /
                                   denominator)};
        }
        fft.inverse(response);
        for (int64_t i = 0; i < fftLength; ++i)
            impulse[static_cast<size_t>(i)] =
                response[static_cast<size_t>(i)].real();

        int64_t start = 0;
        if (options.trimToPeak) {
            int64_t peakIndex = 0;
            float peak = 0.0f;
            for (int64_t i = 0; i < fftLength; ++i) {
                const float magnitude = std::abs(impulse[static_cast<size_t>(i)]);
                if (magnitude > peak) {
                    peak = magnitude;
                    peakIndex = i;
                }
            }
            const int64_t preDelay = static_cast<int64_t>(
                std::llround(options.preDelaySeconds * recorded.sampleRate()));
            start = peakIndex - preDelay;
        }

        for (int64_t i = 0; i < irLength; ++i) {
            const int64_t index = start + i;
            result.sample(ch, i) =
                index >= 0 && index < fftLength
                    ? impulse[static_cast<size_t>(index)]
                    : 0.0f;
        }

        if (options.normalize) {
            float peak = 0.0f;
            for (int64_t i = 0; i < irLength; ++i)
                peak = std::max(peak, std::abs(result.sample(ch, i)));
            if (peak > 1e-12f) {
                const float gain = 1.0f / peak;
                for (int64_t i = 0; i < irLength; ++i)
                    result.sample(ch, i) *= gain;
            }
        }
    }
    return result;
}

bool writeCaptureManifest(const std::string& path, const CaptureInfo& info,
                          std::string* error) {
    if (error) error->clear();
    try {
        nlohmann::json root;
        root["format"] = "vwave-capture";
        root["version"] = 1;
        root["kind"] = "linear-impulse-response";
        root["name"] = info.name;
        root["source"] = info.source;
        root["notes"] = info.notes;
        root["sampleRate"] = info.sampleRate;
        root["frames"] = info.frames;
        root["sweep"] = {
            {"startHz", info.sweep.startHz},
            {"endHz", info.sweep.endHz},
            {"seconds", info.sweep.seconds},
            {"amplitude", info.sweep.amplitude},
            {"sampleRate", info.sweep.sampleRate},
            {"channels", info.sweep.channels}};
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            setError(error, "cannot create capture manifest");
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            setError(error, "failed while writing capture manifest");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot write capture manifest: ") +
                            e.what());
        return false;
    }
}

} // namespace vwave
