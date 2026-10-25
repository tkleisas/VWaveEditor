#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

enum class WavetableMorphMode {
    Linear,
    EqualPower,
    PhaseAligned,
    Spectral,
    Harmonic,
};

enum class WavetableInterpolation {
    Nearest,
    Linear,
    Cubic,
};

const char* wavetableMorphModeName(WavetableMorphMode mode);
bool wavetableMorphModeFromName(const std::string& name,
                                WavetableMorphMode& mode);
const char* wavetableInterpolationName(WavetableInterpolation interpolation);
bool wavetableInterpolationFromName(const std::string& name,
                                    WavetableInterpolation& interpolation);

struct WavetableDesignOptions {
    WavetableMorphMode morphMode = WavetableMorphMode::Linear;
    WavetableInterpolation interpolation = WavetableInterpolation::Linear;
    double rootNote = 60.0;
    int oversampling = 1;
    bool bandLimited = false;
    int maxHarmonic = 0;
    int quantizationBits = 0;
    bool normalized = false;
    bool dcRemoved = false;
    bool phaseAligned = false;
};

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

    const WavetableDesignOptions& designOptions() const {
        return m_options;
    }
    void setDesignOptions(const WavetableDesignOptions& options);
    WavetableMorphMode morphMode() const { return m_options.morphMode; }
    void setMorphMode(WavetableMorphMode mode);
    WavetableInterpolation interpolation() const {
        return m_options.interpolation;
    }
    void setInterpolation(WavetableInterpolation interpolation);
    double rootNote() const { return m_options.rootNote; }
    void setRootNote(double midiNote);
    int oversampling() const { return m_options.oversampling; }
    void setOversampling(int factor);

    const std::vector<float>& frame(int index) const;
    std::vector<float>& frame(int index);
    const std::vector<double>& framePositions() const {
        return m_framePositions;
    }
    double framePosition(int index) const;

    bool setFrame(int index, const std::vector<float>& samples);
    bool addFrame(const std::vector<float>& samples);
    bool addKeyframe(const std::vector<float>& samples, double position);
    bool setFramePosition(int index, double position);
    bool setFramePositions(const std::vector<double>& positions);
    bool setFrameFromHarmonics(int index,
                               const std::vector<double>& amplitudes,
                               const std::vector<double>& phases = {},
                               bool normalize = false);
    bool removeFrame(int index);
    void clear();

    float sample(double framePosition, double phase) const;
    float sampleMorph(double position, double phase) const;
    float sampleBlend(const std::vector<double>& weights,
                      double phase) const;
    std::vector<float> morphedFrame(double position) const;
    std::vector<float> blendedFrame(const std::vector<double>& weights) const;

    void normalizeFrames(float peak = 0.999f);
    void removeDc();
    void phaseAlignFrames();
    void bandLimitFrames(int maxHarmonic = 0);
    void quantizeFrames(int bits);
    void randomizePhases(uint32_t seed);

    Wavetable resampled(int newFrameSize, int maxFrames = 0) const;

    bool save(const std::string& path, std::string* error = nullptr) const;
    static bool load(const std::string& path, Wavetable& out,
                     std::string* error = nullptr);
    static bool loadAuto(const std::string& path, Wavetable& out,
                         std::string* error = nullptr);
    bool saveWav(const std::string& path, int sampleRate,
                 std::string* error = nullptr) const;
    static bool loadWav(const std::string& path, int frameSize, Wavetable& out,
                        std::string* error = nullptr);

    static Wavetable fromHarmonics(const std::vector<double>& amplitudes,
                                   int frameSize = 2048,
                                   std::string name = {});
    static Wavetable fromHarmonics(const std::vector<double>& amplitudes,
                                   const std::vector<double>& phases,
                                   int frameSize, std::string name,
                                   bool normalize = true);

private:
    void invalidateCaches();
    void rebuildDefaultFramePositions();
    void ensureAlignedFrames() const;
    void ensureMorphCache() const;
    void ensureBlendCache(const std::vector<double>& weights) const;
    float sampleFrameAtPhase(const std::vector<float>& frame,
                             double phase) const;
    float sampleFrameAtPhase(const float* frame, int frameSize,
                             double phase) const;
    float sampleMorphedPosition(double position, double phase) const;

    int m_frameSize = 0;
    int m_sampleRate = 48000;
    std::string m_name;
    std::vector<std::vector<float>> m_frames;
    std::vector<double> m_framePositions;
    WavetableDesignOptions m_options;

    mutable std::vector<std::vector<float>> m_alignedFrames;
    mutable bool m_alignedValid = false;
    mutable std::vector<float> m_morphCache;
    mutable bool m_morphCacheValid = false;
    mutable std::vector<double> m_blendCacheWeights;
    mutable std::vector<float> m_blendCacheFrame;
    mutable bool m_blendCacheValid = false;
};

Wavetable extractWavetable(const Audio& audio,
                           const WavetableExtractOptions& options,
                           std::string* error = nullptr);

} // namespace vwave
