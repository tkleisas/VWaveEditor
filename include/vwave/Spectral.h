#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct Spectrogram {
    int fftSize = 2048;
    int hopSize = 512;
    int sampleRate = 48000;
    int frameCount = 0;
    int binCount = 0;
    double minDb = -120.0;
    double maxDb = 0.0;
    std::vector<float> magnitudes;
    std::vector<float> decibels;

    float magnitudeAt(int frame, int bin) const;
    float dbAt(int frame, int bin) const;
    double binFrequency(int bin) const;
    double frameSeconds(int frame) const;
};

struct PhaseSpectrogram {
    int fftSize = 2048;
    int hopSize = 512;
    int sampleRate = 48000;
    int frameCount = 0;
    int binCount = 0;
    double minDb = -120.0;
    double maxDb = 0.0;
    std::vector<float> decibels;
    std::vector<float> phases;
    std::vector<float> instantaneousHz;
    std::vector<float> confidence;

    bool valid() const {
        return frameCount > 0 && binCount > 0 && fftSize > 0;
    }
    float dbAt(int frame, int bin) const;
    float phaseAt(int frame, int bin) const;
    float instantaneousHzAt(int frame, int bin) const;
    float confidenceAt(int frame, int bin) const;
    double binFrequency(int bin) const;
    double frameSeconds(int frame) const;
};

Spectrogram computeSpectrogram(const Audio& audio, int fftSize = 2048,
                               int hopSize = 512, Range range = {});

PhaseSpectrogram computePhaseSpectrogram(const Audio& audio,
                                         int fftSize = 2048,
                                         int maxFrames = 1536,
                                         Range range = {});

Audio spectralGain(const Audio& audio, double lowHz, double highHz,
                   double gainDb, Range range = {}, int fftSize = 2048,
                   int hopSize = 512);
Audio spectralBandGains(const Audio& audio,
                        const std::vector<double>& bandEdges,
                        const std::vector<double>& gainsDb, Range range = {},
                        int fftSize = 2048, int hopSize = 512);
Audio spectralFilter(const Audio& audio, const std::string& type,
                     double cutoffHz, Range range = {}, int order = 4,
                     int fftSize = 2048, int hopSize = 512);
Audio spectralGate(const Audio& audio, double thresholdDb, Range range = {},
                   int fftSize = 2048, int hopSize = 512);
Audio spectralBlur(const Audio& audio, int timeRadius, int frequencyRadius,
                   Range range = {}, int fftSize = 2048, int hopSize = 512);

} // namespace vwave
