#pragma once

#include <string>

namespace vwave {

struct NoteInfo {
    int midi = 60;
    double frequency = 261.6255653005986;
    double cents = 0.0;
    std::string name = "C4";
};

double midiNoteFrequency(double note, double a4 = 440.0);
double frequencyToMidi(double frequency, double a4 = 440.0);
std::string midiNoteName(int midi);
NoteInfo nearestNote(double frequency, double a4 = 440.0);

} // namespace vwave
