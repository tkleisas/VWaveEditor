#include "vwave/Wavetable.h"

#include "Base64.h"
#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"
#include "vwave/FFT.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMorphCacheSteps = 129;

std::string sidecarPath(const std::string& path) {
    return path + ".json";
}

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return extension;
}

float averagedSample(const Audio& audio, int64_t frame) {
    if (frame < 0 || frame >= audio.numFrames() || audio.numChannels() <= 0)
        return 0.0f;
    double sum = 0.0;
    for (int ch = 0; ch < audio.numChannels(); ++ch)
        sum += audio.sample(ch, frame);
    return static_cast<float>(sum / static_cast<double>(audio.numChannels()));
}

std::vector<float> resampleCycle(const Audio& audio, int64_t start,
                                 int64_t cycleLength, int frameSize) {
    std::vector<float> frame(static_cast<size_t>(frameSize), 0.0f);
    if (cycleLength <= 0 || frameSize <= 0) return frame;
    for (int i = 0; i < frameSize; ++i) {
        const double position =
            static_cast<double>(start) +
            static_cast<double>(i) * static_cast<double>(cycleLength) /
                static_cast<double>(frameSize);
        const int64_t index = static_cast<int64_t>(std::floor(position));
        const double fraction = position - static_cast<double>(index);
        const float a = averagedSample(audio, index);
        const float b = averagedSample(audio, index + 1);
        frame[static_cast<size_t>(i)] =
            static_cast<float>(a + (b - a) * fraction);
    }
    return frame;
}

void alignPhase(std::vector<float>& frame) {
    if (frame.size() < 2) return;
    size_t crossing = 0;
    for (size_t i = 1; i < frame.size(); ++i) {
        if (frame[i - 1] < 0.0f && frame[i] >= 0.0f) {
            crossing = i;
            break;
        }
    }
    if (crossing == 0) return;
    std::rotate(frame.begin(),
                frame.begin() + static_cast<std::ptrdiff_t>(crossing),
                frame.end());
}

std::string formatError(const std::string& prefix,
                        const std::exception& error) {
    return prefix + error.what();
}

int wrapIndex(int index, int size) {
    if (size <= 0) return 0;
    index %= size;
    if (index < 0) index += size;
    return index;
}

std::vector<int> orderedFrames(const std::vector<double>& positions,
                               int count) {
    std::vector<int> order(static_cast<size_t>(count));
    std::iota(order.begin(), order.end(), 0);
    if (static_cast<int>(positions.size()) == count) {
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return positions[static_cast<size_t>(a)] <
                   positions[static_cast<size_t>(b)];
        });
    }
    return order;
}

bool findFramePair(const std::vector<double>& positions, int count, double t,
                   int& left, int& right, double& mix) {
    if (count <= 0) return false;
    if (count == 1) {
        left = right = 0;
        mix = 0.0;
        return true;
    }
    const double clamped = std::clamp(t, 0.0, 1.0);
    if (static_cast<int>(positions.size()) != count) {
        const double position = clamped * static_cast<double>(count - 1);
        left = static_cast<int>(std::floor(position));
        right = std::min(left + 1, count - 1);
        mix = position - static_cast<double>(left);
        return true;
    }
    const std::vector<int> order = orderedFrames(positions, count);
    if (clamped <= positions[static_cast<size_t>(order.front())]) {
        left = right = order.front();
        mix = 0.0;
        return true;
    }
    if (clamped >= positions[static_cast<size_t>(order.back())]) {
        left = right = order.back();
        mix = 0.0;
        return true;
    }
    for (int i = 0; i + 1 < count; ++i) {
        const int a = order[static_cast<size_t>(i)];
        const int b = order[static_cast<size_t>(i + 1)];
        const double p0 = positions[static_cast<size_t>(a)];
        const double p1 = positions[static_cast<size_t>(b)];
        if (clamped <= p1) {
            left = a;
            right = b;
            mix = p1 > p0 ? (clamped - p0) / (p1 - p0) : 0.0;
            return true;
        }
    }
    left = right = order.back();
    mix = 0.0;
    return true;
}

int fftSizeFor(int frameSize) {
    return nextPowerOfTwo(std::max(2, frameSize));
}

int bestCircularLag(const std::vector<float>& reference,
                    const std::vector<float>& frame) {
    const int size = std::min<int>(static_cast<int>(reference.size()),
                                   static_cast<int>(frame.size()));
    if (size < 2) return 0;
    const int fftSize = fftSizeFor(size);
    FFT fft(fftSize);
    std::vector<std::complex<float>> a(static_cast<size_t>(fftSize));
    std::vector<std::complex<float>> b(static_cast<size_t>(fftSize));
    for (int i = 0; i < size; ++i) {
        a[static_cast<size_t>(i)] = {reference[static_cast<size_t>(i)], 0.0f};
        b[static_cast<size_t>(i)] = {frame[static_cast<size_t>(i)], 0.0f};
    }
    fft.forward(a);
    fft.forward(b);
    for (int i = 0; i < fftSize; ++i)
        a[static_cast<size_t>(i)] *= std::conj(b[static_cast<size_t>(i)]);
    fft.inverse(a);
    int bestLag = 0;
    float best = -std::numeric_limits<float>::infinity();
    for (int lag = 0; lag < size; ++lag) {
        const float value = a[static_cast<size_t>(lag)].real();
        if (value > best) {
            best = value;
            bestLag = lag;
        }
    }
    return wrapIndex(-bestLag, size);
}

std::vector<float> rotateLeft(const std::vector<float>& frame, int lag) {
    const int size = static_cast<int>(frame.size());
    std::vector<float> out(frame.size(), 0.0f);
    if (size <= 0) return out;
    for (int i = 0; i < size; ++i)
        out[static_cast<size_t>(i)] =
            frame[static_cast<size_t>(wrapIndex(i + lag, size))];
    return out;
}

std::vector<float> spectralMorph(const std::vector<float>& a,
                                 const std::vector<float>& b, double mix,
                                 bool harmonic) {
    const int size = static_cast<int>(std::min(a.size(), b.size()));
    if (size <= 0) return {};
    const int fftSize = fftSizeFor(size);
    FFT fft(fftSize);
    std::vector<std::complex<float>> left(static_cast<size_t>(fftSize));
    std::vector<std::complex<float>> right(static_cast<size_t>(fftSize));
    for (int i = 0; i < size; ++i) {
        left[static_cast<size_t>(i)] = {a[static_cast<size_t>(i)], 0.0f};
        right[static_cast<size_t>(i)] = {b[static_cast<size_t>(i)], 0.0f};
    }
    fft.forward(left);
    fft.forward(right);
    const double t = std::clamp(mix, 0.0, 1.0);
    std::vector<std::complex<float>> combined(static_cast<size_t>(fftSize));
    for (int bin = 0; bin <= fftSize / 2; ++bin) {
        std::complex<float> value;
        if (harmonic) {
            const float magnitudeA = std::abs(left[static_cast<size_t>(bin)]);
            const float magnitudeB = std::abs(right[static_cast<size_t>(bin)]);
            const float magnitude = static_cast<float>(
                (1.0 - t) * magnitudeA + t * magnitudeB);
            const std::complex<float>& phaseSource =
                magnitudeA >= magnitudeB ? left[static_cast<size_t>(bin)]
                                         : right[static_cast<size_t>(bin)];
            const float phase = std::arg(phaseSource);
            value = std::polar(magnitude, phase);
        } else {
            value = left[static_cast<size_t>(bin)] *
                        static_cast<float>(1.0 - t) +
                    right[static_cast<size_t>(bin)] * static_cast<float>(t);
        }
        combined[static_cast<size_t>(bin)] = value;
        if (bin > 0 && bin < fftSize / 2)
            combined[static_cast<size_t>(fftSize - bin)] =
                std::conj(value);
    }
    combined[0] = {combined[0].real(), 0.0f};
    combined[static_cast<size_t>(fftSize / 2)] = {
        combined[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
    fft.inverse(combined);
    std::vector<float> out(static_cast<size_t>(size), 0.0f);
    for (int i = 0; i < size; ++i)
        out[static_cast<size_t>(i)] =
            combined[static_cast<size_t>(i)].real();
    return out;
}

std::vector<float> blendSpectral(const std::vector<std::vector<float>>& frames,
                                 const std::vector<double>& weights,
                                 int frameSize, bool harmonic) {
    if (frameSize <= 0) return {};
    const int fftSize = fftSizeFor(frameSize);
    FFT fft(fftSize);
    std::vector<std::complex<float>> accumulated(
        static_cast<size_t>(fftSize), {0.0f, 0.0f});
    std::vector<float> magnitudes(static_cast<size_t>(fftSize / 2 + 1), 0.0f);
    std::vector<float> phases(static_cast<size_t>(fftSize / 2 + 1), 0.0f);
    std::vector<double> phaseWeights(static_cast<size_t>(fftSize / 2 + 1),
                                     -1.0);
    for (size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const double weight = weights[frameIndex];
        if (weight == 0.0) continue;
        std::vector<std::complex<float>> spectrum(
            static_cast<size_t>(fftSize), {0.0f, 0.0f});
        for (int i = 0; i < frameSize; ++i)
            spectrum[static_cast<size_t>(i)] = {
                frames[frameIndex][static_cast<size_t>(i)], 0.0f};
        fft.forward(spectrum);
        if (harmonic) {
            const double magnitudeWeight = std::abs(weight);
            for (int bin = 0; bin <= fftSize / 2; ++bin) {
                const auto& value = spectrum[static_cast<size_t>(bin)];
                magnitudes[static_cast<size_t>(bin)] += static_cast<float>(
                    magnitudeWeight * std::abs(value));
                if (magnitudeWeight > phaseWeights[static_cast<size_t>(bin)]) {
                    phaseWeights[static_cast<size_t>(bin)] = magnitudeWeight;
                    phases[static_cast<size_t>(bin)] =
                        std::arg(value) + (weight < 0.0 ? kPi : 0.0);
                }
            }
        } else {
            for (int bin = 0; bin < fftSize; ++bin)
                accumulated[static_cast<size_t>(bin)] +=
                    spectrum[static_cast<size_t>(bin)] *
                    static_cast<float>(weight);
        }
    }
    if (harmonic) {
        for (int bin = 0; bin <= fftSize / 2; ++bin) {
            accumulated[static_cast<size_t>(bin)] =
                std::polar(magnitudes[static_cast<size_t>(bin)],
                           phases[static_cast<size_t>(bin)]);
            if (bin > 0 && bin < fftSize / 2)
                accumulated[static_cast<size_t>(fftSize - bin)] =
                    std::conj(accumulated[static_cast<size_t>(bin)]);
        }
        accumulated[0] = {accumulated[0].real(), 0.0f};
        accumulated[static_cast<size_t>(fftSize / 2)] = {
            accumulated[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
    }
    fft.inverse(accumulated);
    std::vector<float> out(static_cast<size_t>(frameSize), 0.0f);
    for (int i = 0; i < frameSize; ++i)
        out[static_cast<size_t>(i)] =
            accumulated[static_cast<size_t>(i)].real();
    return out;
}

nlohmann::json metadataJson(const Wavetable& table, bool includeData) {
    nlohmann::json root;
    root["format"] = "vwave-wavetable";
    root["version"] = 2;
    root["frameSize"] = table.frameSize();
    root["frameCount"] = table.frameCount();
    root["sampleRate"] = table.sampleRate();
    root["name"] = table.name();
    root["rootNote"] = table.rootNote();
    root["morphMode"] = wavetableMorphModeName(table.designOptions().morphMode);
    root["interpolation"] =
        wavetableInterpolationName(table.designOptions().interpolation);
    root["keyframePositions"] = table.framePositions();
    root["oversampling"] = table.designOptions().oversampling;
    root["bandLimited"] = table.designOptions().bandLimited;
    root["maxHarmonic"] = table.designOptions().maxHarmonic;
    root["quantizationBits"] = table.designOptions().quantizationBits;
    root["normalized"] = table.designOptions().normalized;
    root["dcRemoved"] = table.designOptions().dcRemoved;
    root["phaseAligned"] = table.designOptions().phaseAligned;
    if (includeData && table.valid()) {
        std::vector<float> flattened;
        flattened.reserve(static_cast<size_t>(table.frameCount() *
                                             table.frameSize()));
        for (int frameIndex = 0; frameIndex < table.frameCount(); ++frameIndex) {
            const auto& frame = table.frame(frameIndex);
            flattened.insert(flattened.end(), frame.begin(), frame.end());
        }
        root["data"] = base64Encode(
            reinterpret_cast<const uint8_t*>(flattened.data()),
            flattened.size() * sizeof(float));
    }
    return root;
}

bool applyMetadata(const nlohmann::json& root, Wavetable& table,
                   std::string* error) {
    table.setSampleRate(root.value("sampleRate", 48000));
    table.setName(root.value("name", std::string{}));
    WavetableDesignOptions options = table.designOptions();
    const std::string morph =
        root.value("morphMode", std::string{"linear"});
    if (!wavetableMorphModeFromName(morph, options.morphMode)) {
        if (error) *error = "unknown wavetable morph mode: " + morph;
        return false;
    }
    const std::string interpolation =
        root.value("interpolation", std::string{"linear"});
    if (!wavetableInterpolationFromName(interpolation, options.interpolation)) {
        if (error) *error = "unknown wavetable interpolation: " + interpolation;
        return false;
    }
    options.rootNote = root.value("rootNote", options.rootNote);
    options.oversampling = root.value("oversampling", options.oversampling);
    options.bandLimited = root.value("bandLimited", false);
    options.maxHarmonic = root.value("maxHarmonic", 0);
    options.quantizationBits = root.value("quantizationBits", 0);
    options.normalized = root.value("normalized", false);
    options.dcRemoved = root.value("dcRemoved", false);
    options.phaseAligned = root.value("phaseAligned", false);
    table.setDesignOptions(options);
    if (root.contains("keyframePositions") &&
        root["keyframePositions"].is_array() &&
        static_cast<int>(root["keyframePositions"].size()) ==
            table.frameCount()) {
        std::vector<double> positions;
        positions.reserve(root["keyframePositions"].size());
        for (const auto& value : root["keyframePositions"])
            positions.push_back(value.get<double>());
        table.setFramePositions(positions);
    }
    return true;
}

bool readJsonFile(const std::string& path, nlohmann::json& root) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in >> root;
    return true;
}

} // namespace

const char* wavetableMorphModeName(WavetableMorphMode mode) {
    switch (mode) {
    case WavetableMorphMode::Linear: return "linear";
    case WavetableMorphMode::EqualPower: return "equal_power";
    case WavetableMorphMode::PhaseAligned: return "phase_aligned";
    case WavetableMorphMode::Spectral: return "spectral";
    case WavetableMorphMode::Harmonic: return "harmonic";
    }
    return "linear";
}

bool wavetableMorphModeFromName(const std::string& name,
                                WavetableMorphMode& mode) {
    if (name == "linear") mode = WavetableMorphMode::Linear;
    else if (name == "equal_power" || name == "equal-power")
        mode = WavetableMorphMode::EqualPower;
    else if (name == "phase_aligned" || name == "phase-aligned")
        mode = WavetableMorphMode::PhaseAligned;
    else if (name == "spectral") mode = WavetableMorphMode::Spectral;
    else if (name == "harmonic") mode = WavetableMorphMode::Harmonic;
    else return false;
    return true;
}

const char* wavetableInterpolationName(WavetableInterpolation interpolation) {
    switch (interpolation) {
    case WavetableInterpolation::Nearest: return "nearest";
    case WavetableInterpolation::Linear: return "linear";
    case WavetableInterpolation::Cubic: return "cubic";
    }
    return "linear";
}

bool wavetableInterpolationFromName(const std::string& name,
                                    WavetableInterpolation& interpolation) {
    if (name == "nearest") interpolation = WavetableInterpolation::Nearest;
    else if (name == "linear") interpolation = WavetableInterpolation::Linear;
    else if (name == "cubic") interpolation = WavetableInterpolation::Cubic;
    else return false;
    return true;
}

Wavetable::Wavetable(int frameSize, std::string name)
    : m_frameSize(frameSize), m_name(std::move(name)) {
    if (frameSize <= 0) m_frameSize = 0;
}

void Wavetable::invalidateCaches() {
    m_alignedFrames.clear();
    m_alignedValid = false;
    m_morphCache.clear();
    m_morphCacheValid = false;
    m_blendCacheWeights.clear();
    m_blendCacheFrame.clear();
    m_blendCacheValid = false;
}

void Wavetable::rebuildDefaultFramePositions() {
    m_framePositions.resize(m_frames.size());
    if (m_frames.size() == 1) {
        m_framePositions[0] = 0.0;
        return;
    }
    for (size_t i = 0; i < m_framePositions.size(); ++i)
        m_framePositions[i] =
            static_cast<double>(i) /
            static_cast<double>(m_framePositions.size() - 1);
}

void Wavetable::setDesignOptions(const WavetableDesignOptions& options) {
    m_options = options;
    if (!std::isfinite(m_options.rootNote)) m_options.rootNote = 60.0;
    m_options.oversampling = std::clamp(m_options.oversampling, 1, 8);
    m_options.quantizationBits =
        std::clamp(m_options.quantizationBits, 0, 24);
    if (m_frameSize > 0)
        m_options.maxHarmonic =
            std::clamp(m_options.maxHarmonic, 0, m_frameSize / 2);
    invalidateCaches();
}

void Wavetable::setMorphMode(WavetableMorphMode mode) {
    m_options.morphMode = mode;
    invalidateCaches();
}

void Wavetable::setInterpolation(WavetableInterpolation interpolation) {
    m_options.interpolation = interpolation;
    invalidateCaches();
}

void Wavetable::setRootNote(double midiNote) {
    if (std::isfinite(midiNote))
        m_options.rootNote = std::clamp(midiNote, 0.0, 127.0);
}

void Wavetable::setOversampling(int factor) {
    m_options.oversampling = std::clamp(factor, 1, 8);
}

const std::vector<float>& Wavetable::frame(int index) const {
    if (index < 0 || index >= frameCount())
        throw std::out_of_range("wavetable frame index out of range");
    return m_frames[static_cast<size_t>(index)];
}

std::vector<float>& Wavetable::frame(int index) {
    if (index < 0 || index >= frameCount())
        throw std::out_of_range("wavetable frame index out of range");
    invalidateCaches();
    return m_frames[static_cast<size_t>(index)];
}

double Wavetable::framePosition(int index) const {
    if (index < 0 || index >= frameCount()) return 0.0;
    if (static_cast<int>(m_framePositions.size()) == frameCount())
        return m_framePositions[static_cast<size_t>(index)];
    if (frameCount() <= 1) return 0.0;
    return static_cast<double>(index) /
           static_cast<double>(frameCount() - 1);
}

bool Wavetable::setFrame(int index, const std::vector<float>& samples) {
    if (index < 0 || index >= frameCount() ||
        static_cast<int>(samples.size()) != m_frameSize)
        return false;
    m_frames[static_cast<size_t>(index)] = samples;
    invalidateCaches();
    return true;
}

bool Wavetable::addFrame(const std::vector<float>& samples) {
    if (m_frameSize <= 0 ||
        static_cast<int>(samples.size()) != m_frameSize)
        return false;
    m_frames.push_back(samples);
    rebuildDefaultFramePositions();
    invalidateCaches();
    return true;
}

bool Wavetable::addKeyframe(const std::vector<float>& samples,
                            double position) {
    if (m_frameSize <= 0 ||
        static_cast<int>(samples.size()) != m_frameSize ||
        !std::isfinite(position))
        return false;
    if (m_frames.empty()) {
        m_frames.push_back(samples);
        m_framePositions.assign(1, std::clamp(position, 0.0, 1.0));
    } else {
        if (static_cast<int>(m_framePositions.size()) != frameCount())
            rebuildDefaultFramePositions();
        m_frames.push_back(samples);
        m_framePositions.push_back(std::clamp(position, 0.0, 1.0));
    }
    invalidateCaches();
    return true;
}

bool Wavetable::setFramePosition(int index, double position) {
    if (index < 0 || index >= frameCount() || !std::isfinite(position))
        return false;
    if (static_cast<int>(m_framePositions.size()) != frameCount())
        rebuildDefaultFramePositions();
    m_framePositions[static_cast<size_t>(index)] =
        std::clamp(position, 0.0, 1.0);
    invalidateCaches();
    return true;
}

bool Wavetable::setFramePositions(const std::vector<double>& positions) {
    if (static_cast<int>(positions.size()) != frameCount()) return false;
    for (double position : positions) {
        if (!std::isfinite(position)) return false;
    }
    m_framePositions = positions;
    for (double& position : m_framePositions)
        position = std::clamp(position, 0.0, 1.0);
    invalidateCaches();
    return true;
}

bool Wavetable::setFrameFromHarmonics(int index,
                                      const std::vector<double>& amplitudes,
                                      const std::vector<double>& phases,
                                      bool normalize) {
    if (index < 0 || index >= frameCount() || amplitudes.size() < 2)
        return false;
    for (double amplitude : amplitudes) {
        if (!std::isfinite(amplitude)) return false;
    }
    for (double phase : phases) {
        if (!std::isfinite(phase)) return false;
    }
    std::vector<float> frameData(static_cast<size_t>(m_frameSize), 0.0f);
    for (int sampleIndex = 0; sampleIndex < m_frameSize; ++sampleIndex) {
        const double phase = 2.0 * kPi * static_cast<double>(sampleIndex) /
                             static_cast<double>(m_frameSize);
        double value = 0.0;
        for (size_t harmonic = 1; harmonic < amplitudes.size(); ++harmonic) {
            const double phaseOffset =
                harmonic < phases.size() && std::isfinite(phases[harmonic])
                    ? phases[harmonic]
                    : 0.0;
            value += amplitudes[harmonic] *
                     std::sin(phase * static_cast<double>(harmonic) + phaseOffset);
        }
        frameData[static_cast<size_t>(sampleIndex)] =
            static_cast<float>(value);
    }
    if (normalize) {
        float peak = 0.0f;
        for (float value : frameData) peak = std::max(peak, std::abs(value));
        if (peak > 1e-12f) {
            const float gain = 0.999f / peak;
            for (float& value : frameData) value *= gain;
        }
        m_options.normalized = true;
    }
    return setFrame(index, frameData);
}

bool Wavetable::removeFrame(int index) {
    if (index < 0 || index >= frameCount()) return false;
    m_frames.erase(m_frames.begin() + index);
    if (static_cast<int>(m_framePositions.size()) == frameCount() + 1)
        m_framePositions.erase(m_framePositions.begin() + index);
    if (m_frames.empty())
        m_framePositions.clear();
    else if (static_cast<int>(m_framePositions.size()) != frameCount())
        rebuildDefaultFramePositions();
    invalidateCaches();
    return true;
}

void Wavetable::clear() {
    m_frames.clear();
    m_framePositions.clear();
    invalidateCaches();
}

float Wavetable::sampleFrameAtPhase(const std::vector<float>& frame,
                                    double phase) const {
    return sampleFrameAtPhase(frame.data(), static_cast<int>(frame.size()),
                              phase);
}

float Wavetable::sampleFrameAtPhase(const float* frame, int size,
                                    double phase) const {
    if (!frame || size <= 0) return 0.0f;
    double wrapped = phase - std::floor(phase);
    if (wrapped < 0.0) wrapped += 1.0;
    const double position = wrapped * static_cast<double>(size);
    if (m_options.interpolation == WavetableInterpolation::Nearest) {
        const int index = wrapIndex(
            static_cast<int>(std::llround(position)), size);
        return frame[static_cast<size_t>(index)];
    }
    if (m_options.interpolation == WavetableInterpolation::Cubic) {
        const int index1 = static_cast<int>(std::floor(position));
        const double mix = position - static_cast<double>(index1);
        const float p0 = frame[static_cast<size_t>(wrapIndex(index1 - 1, size))];
        const float p1 = frame[static_cast<size_t>(wrapIndex(index1, size))];
        const float p2 = frame[static_cast<size_t>(wrapIndex(index1 + 1, size))];
        const float p3 = frame[static_cast<size_t>(wrapIndex(index1 + 2, size))];
        const double t = mix;
        const double t2 = t * t;
        const double t3 = t2 * t;
        return static_cast<float>(
            0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                   (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                   (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3));
    }
    const int index0 = static_cast<int>(std::floor(position));
    const int index1 = wrapIndex(index0 + 1, size);
    const double mix = position - std::floor(position);
    const float a = frame[static_cast<size_t>(wrapIndex(index0, size))];
    const float b = frame[static_cast<size_t>(index1)];
    return static_cast<float>(a + (b - a) * mix);
}

void Wavetable::ensureAlignedFrames() const {
    if (m_alignedValid) return;
    m_alignedFrames.clear();
    if (m_frames.empty()) {
        m_alignedValid = true;
        return;
    }
    m_alignedFrames.push_back(m_frames.front());
    for (size_t i = 1; i < m_frames.size(); ++i) {
        const int lag = bestCircularLag(m_frames.front(), m_frames[i]);
        m_alignedFrames.push_back(rotateLeft(m_frames[i], lag));
    }
    m_alignedValid = true;
}

void Wavetable::ensureMorphCache() const {
    if (m_morphCacheValid) return;
    m_morphCache.assign(
        static_cast<size_t>(kMorphCacheSteps) *
            static_cast<size_t>(std::max(0, m_frameSize)),
        0.0f);
    if (!valid()) {
        m_morphCacheValid = true;
        return;
    }
    for (int step = 0; step < kMorphCacheSteps; ++step) {
        const double position =
            static_cast<double>(step) /
            static_cast<double>(kMorphCacheSteps - 1);
        const std::vector<float> frame = morphedFrame(position);
        std::copy(frame.begin(), frame.end(),
                  m_morphCache.begin() +
                      static_cast<std::ptrdiff_t>(step) * m_frameSize);
    }
    m_morphCacheValid = true;
}

void Wavetable::ensureBlendCache(
    const std::vector<double>& weights) const {
    if (m_blendCacheValid && m_blendCacheWeights == weights) return;
    m_blendCacheFrame = blendedFrame(weights);
    m_blendCacheWeights = weights;
    m_blendCacheValid = true;
}

float Wavetable::sampleMorphedPosition(double position, double phase) const {
    if (!valid()) return 0.0f;
    if (frameCount() == 1)
        return sampleFrameAtPhase(m_frames.front(), phase);
    int left = 0;
    int right = 0;
    double mix = 0.0;
    if (!findFramePair(m_framePositions, frameCount(), position, left, right,
                       mix))
        return 0.0f;
    if (m_options.morphMode == WavetableMorphMode::PhaseAligned)
        ensureAlignedFrames();
    const auto& frames =
        m_options.morphMode == WavetableMorphMode::PhaseAligned
            ? m_alignedFrames
            : m_frames;
    const float a = sampleFrameAtPhase(frames[static_cast<size_t>(left)], phase);
    const float b = sampleFrameAtPhase(frames[static_cast<size_t>(right)], phase);
    if (left == right) return a;
    if (m_options.morphMode == WavetableMorphMode::EqualPower) {
        const double angle = std::clamp(mix, 0.0, 1.0) * kPi / 2.0;
        return static_cast<float>(a * std::cos(angle) + b * std::sin(angle));
    }
    return static_cast<float>(a + (b - a) * std::clamp(mix, 0.0, 1.0));
}

float Wavetable::sampleMorph(double position, double phase) const {
    if (!valid()) return 0.0f;
    if (m_options.morphMode == WavetableMorphMode::Spectral ||
        m_options.morphMode == WavetableMorphMode::Harmonic) {
        ensureMorphCache();
        const double clamped = std::clamp(position, 0.0, 1.0);
        const double cachePosition =
            clamped * static_cast<double>(kMorphCacheSteps - 1);
        const int index0 = static_cast<int>(std::floor(cachePosition));
        const int index1 = std::min(index0 + 1, kMorphCacheSteps - 1);
        const double mix = cachePosition - static_cast<double>(index0);
        const auto* first = m_morphCache.data() +
                            static_cast<std::ptrdiff_t>(index0) * m_frameSize;
        const auto* second = m_morphCache.data() +
                             static_cast<std::ptrdiff_t>(index1) * m_frameSize;
        const float sampleA = sampleFrameAtPhase(first, m_frameSize, phase);
        const float sampleB = sampleFrameAtPhase(second, m_frameSize, phase);
        return static_cast<float>(sampleA + (sampleB - sampleA) * mix);
    }
    return sampleMorphedPosition(position, phase);
}

float Wavetable::sample(double framePosition, double phase) const {
    if (frameCount() <= 0) return 0.0f;
    if (frameCount() == 1) return sampleMorph(0.0, phase);
    const double position =
        framePosition / static_cast<double>(frameCount() - 1);
    return sampleMorph(position, phase);
}

std::vector<float> Wavetable::morphedFrame(double position) const {
    if (!valid()) return {};
    if (frameCount() == 1) return m_frames.front();
    int left = 0;
    int right = 0;
    double mix = 0.0;
    if (!findFramePair(m_framePositions, frameCount(), position, left, right,
                       mix))
        return std::vector<float>(static_cast<size_t>(m_frameSize), 0.0f);
    if (m_options.morphMode == WavetableMorphMode::PhaseAligned)
        ensureAlignedFrames();
    const auto& frames =
        m_options.morphMode == WavetableMorphMode::PhaseAligned
            ? m_alignedFrames
            : m_frames;
    const auto& a = frames[static_cast<size_t>(left)];
    const auto& b = frames[static_cast<size_t>(right)];
    if (left == right) return a;
    const double t = std::clamp(mix, 0.0, 1.0);
    if (m_options.morphMode == WavetableMorphMode::Spectral ||
        m_options.morphMode == WavetableMorphMode::Harmonic) {
        return spectralMorph(
            a, b, t, m_options.morphMode == WavetableMorphMode::Harmonic);
    }
    std::vector<float> out(static_cast<size_t>(m_frameSize), 0.0f);
    if (m_options.morphMode == WavetableMorphMode::EqualPower) {
        const double angle = t * kPi / 2.0;
        const float gainA = static_cast<float>(std::cos(angle));
        const float gainB = static_cast<float>(std::sin(angle));
        for (int i = 0; i < m_frameSize; ++i)
            out[static_cast<size_t>(i)] =
                a[static_cast<size_t>(i)] * gainA +
                b[static_cast<size_t>(i)] * gainB;
        return out;
    }
    for (int i = 0; i < m_frameSize; ++i)
        out[static_cast<size_t>(i)] =
            a[static_cast<size_t>(i)] +
            (b[static_cast<size_t>(i)] - a[static_cast<size_t>(i)]) *
                static_cast<float>(t);
    return out;
}

std::vector<float> Wavetable::blendedFrame(
    const std::vector<double>& weights) const {
    if (!valid() ||
        static_cast<int>(weights.size()) != frameCount())
        return std::vector<float>(static_cast<size_t>(std::max(0, m_frameSize)),
                                  0.0f);
    if (m_options.morphMode == WavetableMorphMode::PhaseAligned)
        ensureAlignedFrames();
    const auto& frames =
        m_options.morphMode == WavetableMorphMode::PhaseAligned
            ? m_alignedFrames
            : m_frames;
    if (m_options.morphMode == WavetableMorphMode::Spectral ||
        m_options.morphMode == WavetableMorphMode::Harmonic) {
        return blendSpectral(
            frames, weights, m_frameSize,
            m_options.morphMode == WavetableMorphMode::Harmonic);
    }
    std::vector<float> out(static_cast<size_t>(m_frameSize), 0.0f);
    double weightSum = 0.0;
    if (m_options.morphMode == WavetableMorphMode::EqualPower) {
        for (double weight : weights) weightSum += std::abs(weight);
        if (weightSum <= 1e-12) return out;
    }
    for (int frameIndex = 0; frameIndex < frameCount(); ++frameIndex) {
        double gain = weights[static_cast<size_t>(frameIndex)];
        if (m_options.morphMode == WavetableMorphMode::EqualPower) {
            if (std::abs(gain) <= 1e-12) continue;
            gain = std::copysign(
                std::sqrt(std::abs(gain) / weightSum), gain);
        }
        if (gain == 0.0) continue;
        const auto& source = frames[static_cast<size_t>(frameIndex)];
        for (int i = 0; i < m_frameSize; ++i)
            out[static_cast<size_t>(i)] += static_cast<float>(
                static_cast<double>(source[static_cast<size_t>(i)]) * gain);
    }
    return out;
}

float Wavetable::sampleBlend(const std::vector<double>& weights,
                             double phase) const {
    if (!valid() ||
        static_cast<int>(weights.size()) != frameCount())
        return 0.0f;
    if (m_options.morphMode == WavetableMorphMode::Spectral ||
        m_options.morphMode == WavetableMorphMode::Harmonic) {
        ensureBlendCache(weights);
        return sampleFrameAtPhase(m_blendCacheFrame, phase);
    }
    if (m_options.morphMode == WavetableMorphMode::PhaseAligned)
        ensureAlignedFrames();
    const auto& frames =
        m_options.morphMode == WavetableMorphMode::PhaseAligned
            ? m_alignedFrames
            : m_frames;
    double weightSum = 0.0;
    if (m_options.morphMode == WavetableMorphMode::EqualPower) {
        for (double weight : weights) weightSum += std::abs(weight);
        if (weightSum <= 1e-12) return 0.0f;
    }
    double result = 0.0;
    for (int frameIndex = 0; frameIndex < frameCount(); ++frameIndex) {
        double gain = weights[static_cast<size_t>(frameIndex)];
        if (m_options.morphMode == WavetableMorphMode::EqualPower) {
            if (std::abs(gain) <= 1e-12) continue;
            gain = std::copysign(
                std::sqrt(std::abs(gain) / weightSum), gain);
        }
        if (gain == 0.0) continue;
        result += static_cast<double>(
                      sampleFrameAtPhase(
                          frames[static_cast<size_t>(frameIndex)], phase)) *
                  gain;
    }
    return static_cast<float>(result);
}

void Wavetable::normalizeFrames(float peak) {
    if (peak <= 0.0f) return;
    for (auto& frameData : m_frames) {
        float framePeak = 0.0f;
        for (float value : frameData)
            framePeak = std::max(framePeak, std::abs(value));
        if (framePeak <= 1e-12f) continue;
        const float gain = peak / framePeak;
        for (float& value : frameData) value *= gain;
    }
    m_options.normalized = true;
    invalidateCaches();
}

void Wavetable::removeDc() {
    for (auto& frameData : m_frames) {
        if (frameData.empty()) continue;
        double sum = 0.0;
        for (float value : frameData) sum += value;
        const float mean =
            static_cast<float>(sum / static_cast<double>(frameData.size()));
        for (float& value : frameData) value -= mean;
    }
    m_options.dcRemoved = true;
    invalidateCaches();
}

void Wavetable::phaseAlignFrames() {
    if (m_frames.size() < 2) {
        m_options.phaseAligned = true;
        return;
    }
    const std::vector<float> reference = m_frames.front();
    for (size_t i = 1; i < m_frames.size(); ++i) {
        const int lag = bestCircularLag(reference, m_frames[i]);
        m_frames[i] = rotateLeft(m_frames[i], lag);
    }
    m_options.phaseAligned = true;
    invalidateCaches();
}

void Wavetable::bandLimitFrames(int maxHarmonic) {
    if (!valid()) return;
    const int limit = maxHarmonic <= 0
                          ? m_frameSize / 2
                          : std::clamp(maxHarmonic, 1, m_frameSize / 2);
    const int fftSize = fftSizeFor(m_frameSize);
    FFT fft(fftSize);
    for (auto& frameData : m_frames) {
        std::vector<std::complex<float>> spectrum(
            static_cast<size_t>(fftSize), {0.0f, 0.0f});
        for (int i = 0; i < m_frameSize; ++i)
            spectrum[static_cast<size_t>(i)] = {
                frameData[static_cast<size_t>(i)], 0.0f};
        fft.forward(spectrum);
        for (int bin = 0; bin <= fftSize / 2; ++bin) {
            if (bin > limit) {
                spectrum[static_cast<size_t>(bin)] = {0.0f, 0.0f};
                if (bin > 0 && bin < fftSize / 2)
                    spectrum[static_cast<size_t>(fftSize - bin)] = {0.0f, 0.0f};
            }
        }
        fft.inverse(spectrum);
        for (int i = 0; i < m_frameSize; ++i)
            frameData[static_cast<size_t>(i)] =
                spectrum[static_cast<size_t>(i)].real();
    }
    m_options.bandLimited = true;
    m_options.maxHarmonic = limit;
    invalidateCaches();
}

void Wavetable::quantizeFrames(int bits) {
    if (bits <= 0) return;
    const int clampedBits = std::clamp(bits, 1, 24);
    const float levels =
        std::pow(2.0f, static_cast<float>(clampedBits - 1));
    for (auto& frameData : m_frames) {
        for (float& value : frameData)
            value = std::round(value * levels) / levels;
    }
    m_options.quantizationBits = clampedBits;
    invalidateCaches();
}

void Wavetable::randomizePhases(uint32_t seed) {
    if (!valid()) return;
    const int fftSize = fftSizeFor(m_frameSize);
    FFT fft(fftSize);
    std::mt19937 generator(seed);
    std::uniform_real_distribution<double> distribution(0.0, 2.0 * kPi);
    for (auto& frameData : m_frames) {
        std::vector<std::complex<float>> spectrum(
            static_cast<size_t>(fftSize), {0.0f, 0.0f});
        for (int i = 0; i < m_frameSize; ++i)
            spectrum[static_cast<size_t>(i)] = {
                frameData[static_cast<size_t>(i)], 0.0f};
        fft.forward(spectrum);
        for (int bin = 1; bin <= fftSize / 2; ++bin) {
            const float magnitude =
                std::abs(spectrum[static_cast<size_t>(bin)]);
            const float phase =
                static_cast<float>(distribution(generator));
            spectrum[static_cast<size_t>(bin)] =
                std::polar(magnitude, phase);
            if (bin < fftSize / 2)
                spectrum[static_cast<size_t>(fftSize - bin)] =
                    std::conj(spectrum[static_cast<size_t>(bin)]);
        }
        spectrum[0] = {spectrum[0].real(), 0.0f};
        spectrum[static_cast<size_t>(fftSize / 2)] = {
            spectrum[static_cast<size_t>(fftSize / 2)].real(), 0.0f};
        fft.inverse(spectrum);
        for (int i = 0; i < m_frameSize; ++i)
            frameData[static_cast<size_t>(i)] =
                spectrum[static_cast<size_t>(i)].real();
    }
    m_options.phaseAligned = false;
    invalidateCaches();
}

Wavetable Wavetable::resampled(int newFrameSize, int maxFrames) const {
    if (!valid() || newFrameSize < 16 || newFrameSize > 65536)
        return Wavetable{};
    std::vector<int> sourceIndices;
    const int count = frameCount();
    if (maxFrames > 0 && count > maxFrames) {
        sourceIndices.reserve(static_cast<size_t>(maxFrames));
        if (maxFrames == 1) {
            sourceIndices.push_back(0);
        } else {
            for (int i = 0; i < maxFrames; ++i) {
                const int index = static_cast<int>(std::llround(
                    static_cast<double>(i) * static_cast<double>(count - 1) /
                    static_cast<double>(maxFrames - 1)));
                sourceIndices.push_back(std::clamp(index, 0, count - 1));
            }
        }
    } else {
        sourceIndices.resize(static_cast<size_t>(count));
        std::iota(sourceIndices.begin(), sourceIndices.end(), 0);
    }
    Wavetable result(newFrameSize, m_name);
    result.setSampleRate(m_sampleRate);
    result.setDesignOptions(m_options);
    std::vector<double> positions;
    positions.reserve(sourceIndices.size());
    for (int sourceIndex : sourceIndices) {
        std::vector<float> frameData(static_cast<size_t>(newFrameSize), 0.0f);
        for (int i = 0; i < newFrameSize; ++i) {
            frameData[static_cast<size_t>(i)] = sampleFrameAtPhase(
                m_frames[static_cast<size_t>(sourceIndex)],
                static_cast<double>(i) / static_cast<double>(newFrameSize));
        }
        result.addKeyframe(frameData, framePosition(sourceIndex));
        positions.push_back(framePosition(sourceIndex));
    }
    result.setFramePositions(positions);
    return result;
}

bool Wavetable::save(const std::string& path, std::string* error) const {
    if (error) error->clear();
    if (!valid()) {
        if (error) *error = "cannot save an empty wavetable";
        return false;
    }
    try {
        const nlohmann::json root = metadataJson(*this, true);
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            if (error) *error = "cannot create wavetable file";
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            if (error) *error = "failed while writing wavetable file";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = formatError("cannot save wavetable: ", e);
        return false;
    }
}

bool Wavetable::load(const std::string& path, Wavetable& out,
                     std::string* error) {
    if (error) error->clear();
    try {
        nlohmann::json root;
        if (!readJsonFile(path, root)) {
            if (error) *error = "cannot open wavetable file";
            return false;
        }
        if (root.value("format", std::string{}) != "vwave-wavetable") {
            if (error) *error = "not a VWaveEditor wavetable";
            return false;
        }
        const int version = root.value("version", 0);
        if (version != 1 && version != 2) {
            if (error) *error = "unsupported wavetable version";
            return false;
        }
        const int frameSize = root.value("frameSize", 0);
        const int frameCount = root.value("frameCount", 0);
        if (frameSize <= 0 || frameCount <= 0) {
            if (error) *error = "invalid wavetable dimensions";
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!base64Decode(root.value("data", std::string{}), bytes)) {
            if (error) *error = "invalid wavetable data";
            return false;
        }
        const size_t expected = static_cast<size_t>(frameSize) *
                                static_cast<size_t>(frameCount) *
                                sizeof(float);
        if (bytes.size() != expected) {
            if (error) *error = "wavetable data size mismatch";
            return false;
        }
        Wavetable loaded(frameSize, root.value("name", std::string{}));
        loaded.setSampleRate(root.value("sampleRate", 48000));
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            std::vector<float> frameData(static_cast<size_t>(frameSize));
            std::memcpy(frameData.data(),
                        bytes.data() + static_cast<size_t>(frameIndex) *
                                           static_cast<size_t>(frameSize) *
                                           sizeof(float),
                        static_cast<size_t>(frameSize) * sizeof(float));
            loaded.addFrame(frameData);
        }
        if (version == 2 && !applyMetadata(root, loaded, error)) return false;
        out = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = formatError("cannot load wavetable: ", e);
        return false;
    }
}

bool Wavetable::loadAuto(const std::string& path, Wavetable& out,
                         std::string* error) {
    const std::string extension = lowerExtension(path);
    if (extension == ".wav" || extension == ".wave")
        return loadWav(path, 2048, out, error);
    return load(path, out, error);
}

bool Wavetable::saveWav(const std::string& path, int sampleRate,
                        std::string* error) const {
    if (error) error->clear();
    if (!valid() || sampleRate <= 0) {
        if (error) *error = "cannot export an empty wavetable";
        return false;
    }
    Audio audio(1, static_cast<int64_t>(frameCount()) * frameSize(),
                sampleRate);
    float* destination = audio.channel(0);
    for (const auto& frameData : m_frames) {
        std::copy(frameData.begin(), frameData.end(), destination);
        destination += frameSize();
    }
    AudioExportOptions options;
    options.format = AudioFormat::Wav;
    options.bitDepth = BitDepth::Float32;
    if (!writeAudio(path, audio, options, error)) return false;
    try {
        const nlohmann::json sidecar = metadataJson(*this, false);
        std::ofstream out(sidecarPath(path), std::ios::binary);
        if (!out) {
            if (error) *error = "cannot create wavetable sidecar";
            return false;
        }
        out << sidecar.dump(2) << '\n';
        if (!out.good()) {
            if (error) *error = "failed while writing wavetable sidecar";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = formatError("cannot save wavetable sidecar: ", e);
        return false;
    }
}

bool Wavetable::loadWav(const std::string& path, int frameSize, Wavetable& out,
                        std::string* error) {
    if (error) error->clear();
    Audio audio;
    if (!readAudio(path, audio, error)) return false;
    if (audio.numChannels() <= 0 || audio.numFrames() <= 0) {
        if (error) *error = "wavetable wav is empty";
        return false;
    }
    int resolvedFrameSize = frameSize;
    nlohmann::json sidecar;
    bool hasSidecar = false;
    try {
        hasSidecar = readJsonFile(sidecarPath(path), sidecar) &&
                     sidecar.value("format", std::string{}) ==
                         "vwave-wavetable";
        if (hasSidecar)
            resolvedFrameSize =
                sidecar.value("frameSize", resolvedFrameSize);
    } catch (...) {
        hasSidecar = false;
    }
    if (resolvedFrameSize <= 0) {
        if (error) *error = "invalid wavetable frame size";
        return false;
    }
    const int64_t frameCount = audio.numFrames() / resolvedFrameSize;
    if (frameCount <= 0) {
        if (error) *error = "wavetable wav is shorter than one frame";
        return false;
    }
    Wavetable loaded(resolvedFrameSize,
                     hasSidecar ? sidecar.value("name", std::string{})
                                : std::string{});
    loaded.setSampleRate(hasSidecar
                             ? sidecar.value("sampleRate", audio.sampleRate())
                             : audio.sampleRate());
    for (int64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        std::vector<float> frameData(static_cast<size_t>(resolvedFrameSize));
        for (int i = 0; i < resolvedFrameSize; ++i) {
            double value = 0.0;
            for (int ch = 0; ch < audio.numChannels(); ++ch)
                value += audio.sample(
                    ch, frameIndex * resolvedFrameSize + i);
            frameData[static_cast<size_t>(i)] =
                static_cast<float>(value / audio.numChannels());
        }
        loaded.addFrame(frameData);
    }
    if (hasSidecar && !applyMetadata(sidecar, loaded, error)) return false;
    out = std::move(loaded);
    return true;
}

Wavetable Wavetable::fromHarmonics(const std::vector<double>& amplitudes,
                                   int frameSize, std::string name) {
    return fromHarmonics(amplitudes, {}, frameSize, std::move(name), true);
}

Wavetable Wavetable::fromHarmonics(const std::vector<double>& amplitudes,
                                   const std::vector<double>& phases,
                                   int frameSize, std::string name,
                                   bool normalize) {
    if (frameSize < 2 || amplitudes.size() < 2) return Wavetable{};
    Wavetable table(frameSize, std::move(name));
    table.addFrame(std::vector<float>(static_cast<size_t>(frameSize), 0.0f));
    if (!table.setFrameFromHarmonics(0, amplitudes, phases, normalize))
        return Wavetable{};
    return table;
}

Wavetable extractWavetable(const Audio& audio,
                           const WavetableExtractOptions& options,
                           std::string* error) {
    if (error) error->clear();
    if (audio.empty()) {
        if (error) *error = "cannot extract a wavetable from empty audio";
        return Wavetable{};
    }
    Range selected = options.range;
    if (selected.length() <= 0) {
        if (selected.start != 0 || selected.end != 0) {
            if (error) *error = "invalid wavetable source range";
            return Wavetable{};
        }
        selected = {0, audio.numFrames()};
    }
    selected = selected.clamped(audio.numFrames());
    if (selected.empty()) {
        if (error) *error = "wavetable source range is empty";
        return Wavetable{};
    }
    const int frameSize = std::clamp(options.frameSize, 64, 65536);
    const int frameCount = std::clamp(options.frameCount, 1, 128);
    int cycleLength = options.cycleLength;
    if (cycleLength <= 0 && options.autoCycle) {
        const PitchEstimate pitch = detectPitch(audio, selected);
        if (pitch.frequency > 0.0) {
            cycleLength = static_cast<int>(std::llround(
                static_cast<double>(audio.sampleRate()) / pitch.frequency));
        }
    }
    if (cycleLength <= 0) {
        cycleLength = static_cast<int>(
            std::max<int64_t>(64, selected.length() / frameCount));
    }
    const int64_t minimumCycle = std::min<int64_t>(32, selected.length());
    cycleLength = static_cast<int>(std::clamp<int64_t>(
        cycleLength, minimumCycle, selected.length()));

    Wavetable table(frameSize,
                    options.name.empty() ? std::string("Wavetable")
                                         : options.name);
    table.setSampleRate(audio.sampleRate());
    const int64_t maxStart = selected.end - cycleLength;
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        int64_t start = selected.start;
        if (frameCount > 1) {
            start = selected.start + static_cast<int64_t>(std::llround(
                static_cast<double>(maxStart - selected.start) *
                static_cast<double>(frameIndex) /
                static_cast<double>(frameCount - 1)));
        }
        start = std::clamp<int64_t>(start, selected.start,
                                    std::max(selected.start, maxStart));
        std::vector<float> frameData =
            resampleCycle(audio, start, cycleLength, frameSize);
        if (options.phaseAlign) alignPhase(frameData);
        if (options.removeDc) {
            double sum = 0.0;
            for (float value : frameData) sum += value;
            const float mean = static_cast<float>(
                sum / static_cast<double>(frameData.size()));
            for (float& value : frameData) value -= mean;
        }
        table.addFrame(frameData);
    }
    if (options.normalizeFrames) table.normalizeFrames();
    WavetableDesignOptions design = table.designOptions();
    design.normalized = options.normalizeFrames;
    design.dcRemoved = options.removeDc;
    design.phaseAligned = false;
    table.setDesignOptions(design);
    return table;
}

} // namespace vwave
