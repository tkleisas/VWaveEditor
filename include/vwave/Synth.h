#pragma once

#include "vwave/Audio.h"
#include "vwave/Wavetable.h"

namespace vwave {

struct Adsr {
    double attack = 0.005;
    double decay = 0.12;
    double sustain = 0.75;
    double release = 0.25;
};

struct SynthNoteOptions {
    int sampleRate = 0;
    int channels = 1;
    double rootNote = 69.0;
    double morph = 0.0;
    double gain = 0.8;
    int unison = 1;
    double detuneCents = 0.0;
    int oversampling = 0;
    bool useWavetableRootNote = false;
    std::vector<double> blendWeights;
    Adsr envelope;
};

Audio renderWavetableNote(const Wavetable& wavetable, double midiNote,
                          double durationSeconds,
                          const SynthNoteOptions& options = {});

} // namespace vwave
