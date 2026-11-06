#include "vwave/Music.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vwave {

namespace {

const char* kNames[] = {"C", "C#", "D", "D#", "E", "F",
                        "F#", "G", "G#", "A", "A#", "B"};

} // namespace

double midiNoteFrequency(double note, double a4) {
    return a4 * std::pow(2.0, (note - 69.0) / 12.0);
}

double frequencyToMidi(double frequency, double a4) {
    if (!std::isfinite(frequency) || frequency <= 0.0 || a4 <= 0.0)
        return 0.0;
    return 69.0 + 12.0 * std::log2(frequency / a4);
}

std::string midiNoteName(int midi) {
    midi = std::clamp(midi, 0, 127);
    return std::string(kNames[midi % 12]) + std::to_string(midi / 12 - 1);
}

NoteInfo nearestNote(double frequency, double a4) {
    NoteInfo info;
    if (!std::isfinite(frequency) || frequency <= 0.0 || a4 <= 0.0) {
        info.frequency = midiNoteFrequency(60.0, a4);
        info.name = midiNoteName(60);
        return info;
    }
    const double midi = frequencyToMidi(frequency, a4);
    info.midi = std::clamp(static_cast<int>(std::llround(midi)), 0, 127);
    info.frequency = midiNoteFrequency(info.midi, a4);
    info.cents = 1200.0 * std::log2(frequency / info.frequency);
    info.name = midiNoteName(info.midi);
    return info;
}

} // namespace vwave
