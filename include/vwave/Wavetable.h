#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct WavetableExtractOptions {
    int frameSize = 2048;
    int frameCount = 8;
    Range range;
    bool autoCycle = true;
    int cycleLength = 0;
    bool normalizeFrames = true;
    bool removeDc = true;
    bool phaseAlign = true;
    std::string name;
};

class Wavetable {
public:
    Wavetable() = default;
    explicit Wavetable(int frameSize, std::string name = {});

    bool valid() const { return m_frameSize > 0 && !m_frames.empty(); }
    int frameSize() const { return m_frameSize; }
    int frameCount() const { return static_cast<int>(m_frames.size()); }
    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int sampleRate) { m_sampleRate = sampleRate; }

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    const std::vector<float>& frame(int index) const;
    std::vector<float>& frame(int index);

    bool setFrame(int index, const std::vector<float>& samples);
    bool addFrame(const std::vector<float>& samples);
    bool removeFrame(int index);
    void clear();

    float sample(double framePosition, double phase) const;
    void normalizeFrames(float peak = 0.999f);
    void removeDc();

    bool save(const std::string& path, std::string* error = nullptr) const;
    static bool load(const std::string& path, Wavetable& out,
                     std::string* error = nullptr);
    bool saveWav(const std::string& path, int sampleRate,
                 std::string* error = nullptr) const;
    static bool loadWav(const std::string& path, int frameSize, Wavetable& out,
                        std::string* error = nullptr);

    static Wavetable fromHarmonics(const std::vector<double>& amplitudes,
                                   int frameSize = 2048,
                                   std::string name = {});

private:
    int m_frameSize = 0;
    int m_sampleRate = 48000;
    std::string m_name;
    std::vector<std::vector<float>> m_frames;
};

Wavetable extractWavetable(const Audio& audio,
                           const WavetableExtractOptions& options,
                           std::string* error = nullptr);

} // namespace vwave
