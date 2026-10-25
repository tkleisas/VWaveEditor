#include "vwave/ImageSpectrogram.h"

#include "vwave/FFT.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

struct Rgba {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 255.0;
};

Rgba sampleImage(const ImageData& image, double u, double v,
                 const ImageSpectrogramOptions& options) {
    double x = options.cropLeft + u * (options.cropRight - options.cropLeft);
    double y = options.cropTop + v * (options.cropBottom - options.cropTop);
    if (options.flipHorizontal) x = 1.0 - x;
    if (options.flipVertical) y = 1.0 - y;
    x = std::clamp(x, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);
    const double px = x * static_cast<double>(image.width - 1);
    const double py = y * static_cast<double>(image.height - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(px)), 0,
                              image.width - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(py)), 0,
                              image.height - 1);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const double fx = px - std::floor(px);
    const double fy = py - std::floor(py);
    auto channel = [&](int xc, int yc, int c) {
        return static_cast<double>(
            image.at(xc, yc, std::min(c, image.channels - 1)));
    };
    Rgba result;
    for (int c = 0; c < image.channels; ++c) {
        const double a = channel(x0, y0, c);
        const double b = channel(x1, y0, c);
        const double d = channel(x0, y1, c);
        const double e = channel(x1, y1, c);
        const double top = a + (b - a) * fx;
        const double bottom = d + (e - d) * fx;
        const double value = top + (bottom - top) * fy;
        if (c == 0) result.r = value;
        else if (c == 1) result.g = value;
        else if (c == 2) result.b = value;
        else result.a = value;
    }
    if (image.channels == 1) {
        result.g = result.r;
        result.b = result.r;
    }
    if (image.channels < 4) result.a = 255.0;
    return result;
}

double hueOf(const Rgba& color) {
    const double r = color.r / 255.0;
    const double g = color.g / 255.0;
    const double b = color.b / 255.0;
    const double maximum = std::max({r, g, b});
    const double minimum = std::min({r, g, b});
    const double delta = maximum - minimum;
    if (delta <= 1e-9) return 0.0;
    double hue = 0.0;
    if (maximum == r) hue = std::fmod((g - b) / delta, 6.0);
    else if (maximum == g) hue = (b - r) / delta + 2.0;
    else hue = (r - g) / delta + 4.0;
    hue /= 6.0;
    if (hue < 0.0) hue += 1.0;
    return hue;
}

double brightnessOf(const Rgba& color,
                    const ImageSpectrogramOptions& options) {
    double value = (0.2126 * color.r + 0.7152 * color.g +
                    0.0722 * color.b) /
                   255.0;
    if (options.useAlpha) value *= color.a / 255.0;
    if (options.invert) value = 1.0 - value;
    value = (value - 0.5) * options.contrast + 0.5;
    value = std::clamp(value, 0.0, 1.0);
    value = std::pow(value, options.gamma);
    if (options.threshold > 0.0) {
        value = value < options.threshold
                    ? 0.0
                    : (value - options.threshold) /
                          std::max(1e-9, 1.0 - options.threshold);
    }
    return std::clamp(value, 0.0, 1.0);
}

double imageX(double t, const ImageSpectrogramOptions& options) {
    if (options.timeMapping == ImageTimeMapping::Mirrored) {
        return t <= 0.5 ? t * 2.0 : (1.0 - t) * 2.0;
    }
    if (options.timeMapping == ImageTimeMapping::Looped) {
        const double loops = static_cast<double>(std::max(1, options.loops));
        return std::fmod(t * loops, 1.0);
    }
    return t;
}

std::vector<double> renderSignal(const std::vector<float>& magnitudes,
                                 const std::vector<float>& phases,
                                 int frameCount, int bins, int fftSize,
                                 int hop, int length,
                                 const std::vector<float>& window) {
    std::vector<double> signal(static_cast<size_t>(length), 0.0);
    std::vector<double> weights(static_cast<size_t>(length), 0.0);
    FFT fft(fftSize);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));
    for (int frame = 0; frame < frameCount; ++frame) {
        std::fill(spectrum.begin(), spectrum.end(),
                  std::complex<float>{0.0f, 0.0f});
        const int64_t start = static_cast<int64_t>(frame) * hop;
        for (int bin = 0; bin < bins; ++bin) {
            const size_t index = static_cast<size_t>(frame) *
                                     static_cast<size_t>(bins) +
                                 static_cast<size_t>(bin);
            const float magnitude = magnitudes[index];
            const float phase = phases[index];
            spectrum[static_cast<size_t>(bin)] = {
                magnitude * std::cos(phase), magnitude * std::sin(phase)};
            if (bin > 0 && bin < fftSize / 2)
                spectrum[static_cast<size_t>(fftSize - bin)] =
                    std::conj(spectrum[static_cast<size_t>(bin)]);
        }
        spectrum[0] = {spectrum[0].real(), 0.0f};
        spectrum[static_cast<size_t>(fftSize / 2)] = {
            spectrum[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
        fft.inverse(spectrum);
        for (int i = 0; i < fftSize; ++i) {
            const int64_t destination = start + i;
            if (destination < 0 || destination >= length) continue;
            const double w = window[static_cast<size_t>(i)];
            const double value = spectrum[static_cast<size_t>(i)].real() * w;
            signal[static_cast<size_t>(destination)] += value;
            weights[static_cast<size_t>(destination)] += w * w;
        }
    }
    for (size_t i = 0; i < signal.size(); ++i) {
        if (weights[i] > 1e-9)
            signal[i] /= weights[i];
    }
    return signal;
}

std::vector<float> analyzePhase(const std::vector<double>& signal,
                                int frameCount, int bins, int fftSize, int hop,
                                const std::vector<float>& window) {
    std::vector<float> phases(
        static_cast<size_t>(frameCount) * static_cast<size_t>(bins), 0.0f);
    FFT fft(fftSize);
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize));
    for (int frame = 0; frame < frameCount; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * hop;
        std::fill(spectrum.begin(), spectrum.end(),
                  std::complex<float>{0.0f, 0.0f});
        for (int i = 0; i < fftSize; ++i) {
            const int64_t source = start + i;
            if (source < 0 || source >= static_cast<int64_t>(signal.size()))
                continue;
            spectrum[static_cast<size_t>(i)] = {
                static_cast<float>(signal[static_cast<size_t>(source)] *
                                   window[static_cast<size_t>(i)]),
                0.0f};
        }
        fft.forward(spectrum);
        for (int bin = 0; bin < bins; ++bin) {
            phases[static_cast<size_t>(frame) * static_cast<size_t>(bins) +
                   static_cast<size_t>(bin)] =
                std::arg(spectrum[static_cast<size_t>(bin)]);
        }
    }
    return phases;
}

} // namespace

uint8_t ImageData::at(int x, int y, int channel) const {
    if (!valid() || x < 0 || x >= width || y < 0 || y >= height || channel < 0 ||
        channel >= channels)
        return 0;
    return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) *
                      static_cast<size_t>(channels) +
                  static_cast<size_t>(x) * static_cast<size_t>(channels) +
                  static_cast<size_t>(channel)];
}

bool loadImageFile(const std::string& path, ImageData& out,
                   std::string* error) {
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = "image path is empty";
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
        if (error) {
            const char* reason = stbi_failure_reason();
            *error = reason ? std::string("cannot load image: ") + reason
                            : "cannot load image";
        }
        if (data) stbi_image_free(data);
        return false;
    }
    const size_t count = static_cast<size_t>(width) *
                         static_cast<size_t>(height) * 4u;
    out.width = width;
    out.height = height;
    out.channels = 4;
    out.pixels.assign(data, data + count);
    stbi_image_free(data);
    return true;
}

const char* imageFrequencyScaleName(ImageFrequencyScale scale) {
    switch (scale) {
    case ImageFrequencyScale::Linear: return "linear";
    case ImageFrequencyScale::Logarithmic: return "logarithmic";
    }
    return "logarithmic";
}

bool imageFrequencyScaleFromName(const std::string& name,
                                 ImageFrequencyScale& scale) {
    if (name == "linear") scale = ImageFrequencyScale::Linear;
    else if (name == "log" || name == "logarithmic")
        scale = ImageFrequencyScale::Logarithmic;
    else return false;
    return true;
}

const char* imageTimeMappingName(ImageTimeMapping mapping) {
    switch (mapping) {
    case ImageTimeMapping::Direct: return "direct";
    case ImageTimeMapping::Mirrored: return "mirrored";
    case ImageTimeMapping::Looped: return "looped";
    }
    return "direct";
}

bool imageTimeMappingFromName(const std::string& name,
                              ImageTimeMapping& mapping) {
    if (name == "direct") mapping = ImageTimeMapping::Direct;
    else if (name == "mirrored" || name == "mirror")
        mapping = ImageTimeMapping::Mirrored;
    else if (name == "looped" || name == "loop")
        mapping = ImageTimeMapping::Looped;
    else return false;
    return true;
}

const char* imagePhaseModeName(ImagePhaseMode mode) {
    switch (mode) {
    case ImagePhaseMode::GriffinLim: return "griffin_lim";
    case ImagePhaseMode::FastGriffinLim: return "fast_griffin_lim";
    case ImagePhaseMode::PhaseLocked: return "phase_locked";
    case ImagePhaseMode::Random: return "random";
    case ImagePhaseMode::Color: return "color";
    }
    return "fast_griffin_lim";
}

bool imagePhaseModeFromName(const std::string& name, ImagePhaseMode& mode) {
    if (name == "griffin_lim" || name == "griffin-lim")
        mode = ImagePhaseMode::GriffinLim;
    else if (name == "fast_griffin_lim" || name == "fast_griffin-lim" ||
             name == "fast")
        mode = ImagePhaseMode::FastGriffinLim;
    else if (name == "phase_locked" || name == "phase-locked" ||
             name == "pghi")
        mode = ImagePhaseMode::PhaseLocked;
    else if (name == "random") mode = ImagePhaseMode::Random;
    else if (name == "color" || name == "colour") mode = ImagePhaseMode::Color;
    else return false;
    return true;
}

ImageSpectrogramResult imageToSpectrogram(
    const ImageData& image, const ImageSpectrogramOptions& options,
    std::string* error) {
    if (error) error->clear();
    ImageSpectrogramResult result;
    if (!image.valid()) {
        if (error) *error = "invalid image data";
        return result;
    }
    if (!(options.gamma > 0.0) || !std::isfinite(options.gamma) ||
        !(options.contrast > 0.0) || !std::isfinite(options.contrast) ||
        !(options.threshold >= 0.0 && options.threshold < 1.0) ||
        !(options.cropLeft >= 0.0 && options.cropRight <= 1.0 &&
          options.cropTop >= 0.0 && options.cropBottom <= 1.0 &&
          options.cropRight > options.cropLeft &&
          options.cropBottom > options.cropTop)) {
        if (error) *error = "invalid image mapping options";
        return result;
    }
    const int sampleRate = std::clamp(options.sampleRate, 1000, 192000);
    const int fftSize = nextPowerOfTwo(
        std::clamp(options.fftSize, 256, 8192));
    const int hop = std::clamp(options.hopSize, 64, fftSize);
    const int bins = fftSize / 2 + 1;
    const double minHz = std::max(1.0, options.minHz);
    const double maxHz = std::min(
        static_cast<double>(sampleRate) / 2.0, options.maxHz);
    if (!std::isfinite(minHz) || !std::isfinite(maxHz) ||
        maxHz <= minHz * 1.01) {
        if (error) *error = "invalid image frequency range";
        return result;
    }
    double duration = options.duration;
    if (!(duration > 0.0) || !std::isfinite(duration))
        duration = static_cast<double>(image.width) / 100.0;
    duration = std::clamp(duration, 0.05, 60.0);
    const int64_t totalFrames = std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(duration * sampleRate)));
    const int frameCount = std::max(
        1, static_cast<int>((totalFrames + hop - 1) / hop));
    const int renderLength = (frameCount - 1) * hop + fftSize;
    const int channels = std::clamp(options.channels, 1, 2);
    const int loops = std::clamp(options.loops, 1, 16);

    ImageSpectrogramOptions mapping = options;
    mapping.loops = loops;
    std::vector<float> magnitudes(
        static_cast<size_t>(frameCount) * static_cast<size_t>(bins), 0.0f);
    std::vector<float> initialPhase(
        static_cast<size_t>(frameCount) * static_cast<size_t>(bins), 0.0f);
    std::mt19937 generator(options.seed);
    std::uniform_real_distribution<float> randomPhase(
        -static_cast<float>(kPi), static_cast<float>(kPi));
    const double logRatio = std::log(maxHz / minHz);
    for (int frame = 0; frame < frameCount; ++frame) {
        const double t = frameCount > 1
                             ? static_cast<double>(frame) /
                                   static_cast<double>(frameCount - 1)
                             : 0.0;
        const double u = imageX(t, mapping);
        for (int bin = 0; bin < bins; ++bin) {
            const double frequency =
                static_cast<double>(bin) * sampleRate / fftSize;
            if (frequency < minHz || frequency > maxHz) continue;
            const double frequencyNorm =
                options.frequencyScale == ImageFrequencyScale::Linear
                    ? (frequency - minHz) / (maxHz - minHz)
                    : std::log(frequency / minHz) / logRatio;
            const double v = 1.0 - std::clamp(frequencyNorm, 0.0, 1.0);
            const Rgba color = sampleImage(image, u, v, mapping);
            const double magnitude = brightnessOf(color, mapping);
            const size_t index = static_cast<size_t>(frame) *
                                     static_cast<size_t>(bins) +
                                 static_cast<size_t>(bin);
            magnitudes[index] = static_cast<float>(magnitude);
            initialPhase[index] =
                options.phaseMode == ImagePhaseMode::Color
                    ? static_cast<float>(hueOf(color) * kTwoPi - kPi)
                    : randomPhase(generator);
        }
    }

    if (options.phaseMode == ImagePhaseMode::PhaseLocked) {
        const double expectedStep =
            kTwoPi * static_cast<double>(hop) /
            static_cast<double>(fftSize);
        for (int frame = 1; frame < frameCount; ++frame) {
            for (int bin = 0; bin < bins; ++bin) {
                const size_t index = static_cast<size_t>(frame) *
                                         static_cast<size_t>(bins) +
                                     static_cast<size_t>(bin);
                const size_t previous = static_cast<size_t>(frame - 1) *
                                            static_cast<size_t>(bins) +
                                        static_cast<size_t>(bin);
                if (magnitudes[previous] <= 1e-9f) continue;
                initialPhase[index] = static_cast<float>(std::remainder(
                    static_cast<double>(initialPhase[previous]) +
                        expectedStep * static_cast<double>(bin),
                    kTwoPi));
            }
        }
    }

    std::vector<float> phase = std::move(initialPhase);
    std::vector<float> window = hannWindow(fftSize);
    const int iterations = std::clamp(options.iterations, 0, 128);
    if (options.phaseMode == ImagePhaseMode::GriffinLim ||
        options.phaseMode == ImagePhaseMode::FastGriffinLim ||
        options.phaseMode == ImagePhaseMode::PhaseLocked) {
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const std::vector<double> signal = renderSignal(
                magnitudes, phase, frameCount, bins, fftSize, hop,
                renderLength, window);
            const std::vector<float> updated = analyzePhase(
                signal, frameCount, bins, fftSize, hop, window);
            if (options.phaseMode == ImagePhaseMode::FastGriffinLim ||
                options.phaseMode == ImagePhaseMode::PhaseLocked) {
                const double momentum = std::clamp(options.momentum, 0.0, 0.999);
                for (size_t i = 0; i < phase.size(); ++i) {
                    const double difference =
                        std::remainder(static_cast<double>(updated[i]) -
                                           static_cast<double>(phase[i]),
                                       kTwoPi);
                    phase[i] = static_cast<float>(std::remainder(
                        static_cast<double>(updated[i]) +
                            momentum * difference,
                        kTwoPi));
                }
            } else {
                phase = updated;
            }
        }
    }

    std::vector<double> signal = renderSignal(
        magnitudes, phase, frameCount, bins, fftSize, hop, renderLength,
        window);
    if (static_cast<int>(signal.size()) > totalFrames)
        signal.resize(static_cast<size_t>(totalFrames));
    double peak = 0.0;
    for (double value : signal) peak = std::max(peak, std::abs(value));
    if (options.normalize && peak > 1e-12) {
        const double target = std::clamp(options.peak, 0.0, 1.0);
        const double gain = target / peak;
        for (double& value : signal) value *= gain;
    }

    Audio audio(channels, totalFrames, sampleRate);
    for (int64_t i = 0; i < totalFrames; ++i) {
        const float value = static_cast<float>(
            i < static_cast<int64_t>(signal.size()) ? signal[i] : 0.0);
        for (int ch = 0; ch < channels; ++ch)
            audio.sample(ch, i) = value;
    }
    result.audio = std::move(audio);
    result.imageWidth = image.width;
    result.imageHeight = image.height;
    result.fftSize = fftSize;
    result.hopSize = hop;
    result.frameCount = frameCount;
    result.binCount = bins;
    result.channels = channels;
    result.duration = static_cast<double>(totalFrames) / sampleRate;
    return result;
}

} // namespace vwave
