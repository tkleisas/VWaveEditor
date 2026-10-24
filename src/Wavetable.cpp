#include "vwave/Wavetable.h"

#include "Base64.h"
#include "vwave/Analysis.h"
#include "vwave/AudioIO.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string sidecarPath(const std::string& path) {
    return path + ".json";
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
    std::rotate(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(crossing),
                frame.end());
}

std::string formatError(const std::string& prefix,
                        const std::exception& error) {
    return prefix + error.what();
}

} // namespace

Wavetable::Wavetable(int frameSize, std::string name)
    : m_frameSize(frameSize), m_name(std::move(name)) {
    if (frameSize <= 0) m_frameSize = 0;
}

const std::vector<float>& Wavetable::frame(int index) const {
    if (index < 0 || index >= frameCount())
        throw std::out_of_range("wavetable frame index out of range");
    return m_frames[static_cast<size_t>(index)];
}

std::vector<float>& Wavetable::frame(int index) {
    if (index < 0 || index >= frameCount())
        throw std::out_of_range("wavetable frame index out of range");
    return m_frames[static_cast<size_t>(index)];
}

bool Wavetable::setFrame(int index, const std::vector<float>& samples) {
    if (index < 0 || index >= frameCount() ||
        static_cast<int>(samples.size()) != m_frameSize)
        return false;
    m_frames[static_cast<size_t>(index)] = samples;
    return true;
}

bool Wavetable::addFrame(const std::vector<float>& samples) {
    if (m_frameSize <= 0 ||
        static_cast<int>(samples.size()) != m_frameSize)
        return false;
    m_frames.push_back(samples);
    return true;
}

bool Wavetable::removeFrame(int index) {
    if (index < 0 || index >= frameCount()) return false;
    m_frames.erase(m_frames.begin() + index);
    return true;
}

void Wavetable::clear() { m_frames.clear(); }

float Wavetable::sample(double framePosition, double phase) const {
    if (!valid()) return 0.0f;
    const double clampedPosition =
        std::clamp(framePosition, 0.0, static_cast<double>(frameCount() - 1));
    int frame0 = static_cast<int>(std::floor(clampedPosition));
    int frame1 = frame0;
    double frameMix = 0.0;
    if (frameCount() > 1 && frame0 < frameCount() - 1) {
        frame1 = frame0 + 1;
        frameMix = clampedPosition - static_cast<double>(frame0);
    }
    double wrapped = phase - std::floor(phase);
    if (wrapped < 0.0) wrapped += 1.0;
    const double position = wrapped * static_cast<double>(m_frameSize);
    int index0 = static_cast<int>(std::floor(position));
    if (index0 >= m_frameSize) index0 = 0;
    const int index1 = (index0 + 1) % m_frameSize;
    const double indexMix = position - std::floor(position);
    const auto& a = m_frames[static_cast<size_t>(frame0)];
    const auto& b = m_frames[static_cast<size_t>(frame1)];
    const double sampleA = a[static_cast<size_t>(index0)] +
                           (a[static_cast<size_t>(index1)] -
                            a[static_cast<size_t>(index0)]) *
                               indexMix;
    const double sampleB = b[static_cast<size_t>(index0)] +
                           (b[static_cast<size_t>(index1)] -
                            b[static_cast<size_t>(index0)]) *
                               indexMix;
    return static_cast<float>(sampleA + (sampleB - sampleA) * frameMix);
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
}

bool Wavetable::save(const std::string& path, std::string* error) const {
    if (error) error->clear();
    if (!valid()) {
        if (error) *error = "cannot save an empty wavetable";
        return false;
    }
    try {
        std::vector<float> flattened;
        flattened.reserve(static_cast<size_t>(frameCount() *
                                             frameSize()));
        for (const auto& frameData : m_frames)
            flattened.insert(flattened.end(), frameData.begin(),
                             frameData.end());
        const auto* bytes =
            reinterpret_cast<const uint8_t*>(flattened.data());
        nlohmann::json root;
        root["format"] = "vwave-wavetable";
        root["version"] = 1;
        root["frameSize"] = m_frameSize;
        root["frameCount"] = frameCount();
        root["sampleRate"] = m_sampleRate;
        root["name"] = m_name;
        root["data"] = base64Encode(bytes, flattened.size() * sizeof(float));
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
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error) *error = "cannot open wavetable file";
            return false;
        }
        nlohmann::json root;
        in >> root;
        if (root.value("format", std::string{}) != "vwave-wavetable") {
            if (error) *error = "not a VWaveEditor wavetable";
            return false;
        }
        if (root.value("version", 0) != 1) {
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
        loaded.m_frames.resize(static_cast<size_t>(frameCount));
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            auto& frameData = loaded.m_frames[static_cast<size_t>(frameIndex)];
            frameData.resize(static_cast<size_t>(frameSize));
            std::memcpy(frameData.data(),
                        bytes.data() + static_cast<size_t>(frameIndex) *
                                           static_cast<size_t>(frameSize) *
                                           sizeof(float),
                        static_cast<size_t>(frameSize) * sizeof(float));
        }
        out = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = formatError("cannot load wavetable: ", e);
        return false;
    }
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
        nlohmann::json sidecar;
        sidecar["format"] = "vwave-wavetable";
        sidecar["version"] = 1;
        sidecar["frameSize"] = m_frameSize;
        sidecar["frameCount"] = frameCount();
        sidecar["name"] = m_name;
        sidecar["sampleRate"] = sampleRate;
        std::ofstream out(sidecarPath(path), std::ios::binary);
        if (!out) {
            if (error) *error = "cannot create wavetable sidecar";
            return false;
        }
        out << sidecar.dump(2) << '\n';
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
    std::string name;
    int sampleRate = audio.sampleRate();
    try {
        std::ifstream sidecar(sidecarPath(path));
        if (sidecar) {
            nlohmann::json root;
            sidecar >> root;
            resolvedFrameSize = root.value("frameSize", resolvedFrameSize);
            name = root.value("name", std::string{});
            sampleRate = root.value("sampleRate", sampleRate);
        }
    } catch (...) {
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
    Wavetable loaded(resolvedFrameSize, name);
    loaded.setSampleRate(sampleRate);
    loaded.m_frames.resize(static_cast<size_t>(frameCount));
    for (int64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        auto& frameData = loaded.m_frames[static_cast<size_t>(frameIndex)];
        frameData.resize(static_cast<size_t>(resolvedFrameSize));
        for (int i = 0; i < resolvedFrameSize; ++i) {
            double value = 0.0;
            for (int ch = 0; ch < audio.numChannels(); ++ch)
                value += audio.sample(
                    ch, frameIndex * resolvedFrameSize + i);
            frameData[static_cast<size_t>(i)] =
                static_cast<float>(value / audio.numChannels());
        }
    }
    out = std::move(loaded);
    return true;
}

Wavetable Wavetable::fromHarmonics(const std::vector<double>& amplitudes,
                                   int frameSize, std::string name) {
    if (frameSize < 2 || amplitudes.size() < 2) return Wavetable{};
    Wavetable table(frameSize, std::move(name));
    std::vector<float> frameData(static_cast<size_t>(frameSize), 0.0f);
    for (int sampleIndex = 0; sampleIndex < frameSize; ++sampleIndex) {
        const double phase = 2.0 * kPi * static_cast<double>(sampleIndex) /
                             static_cast<double>(frameSize);
        double value = 0.0;
        for (size_t harmonic = 1; harmonic < amplitudes.size(); ++harmonic) {
            value += amplitudes[harmonic] * std::sin(
                phase * static_cast<double>(harmonic));
        }
        frameData[static_cast<size_t>(sampleIndex)] =
            static_cast<float>(value);
    }
    table.m_frames.push_back(std::move(frameData));
    table.normalizeFrames();
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
    return table;
}

} // namespace vwave
