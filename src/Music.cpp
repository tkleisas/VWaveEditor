#include "vwave/Music.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
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

bool parseNoteName(const std::string& text, int& midi) {
    std::string value;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c)))
            value.push_back(c);
    }
    if (value.size() < 2) return false;
    int semitone = -1;
    const char letter =
        static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    switch (letter) {
    case 'C': semitone = 0; break;
    case 'D': semitone = 2; break;
    case 'E': semitone = 4; break;
    case 'F': semitone = 5; break;
    case 'G': semitone = 7; break;
    case 'A': semitone = 9; break;
    case 'B': semitone = 11; break;
    default: return false;
    }
    size_t position = 1;
    if (position < value.size() &&
        (value[position] == '#' || value[position] == 'b')) {
        semitone += value[position] == '#' ? 1 : -1;
        ++position;
    }
    if (position >= value.size()) return false;
    char* end = nullptr;
    const long octave = std::strtol(value.c_str() + position, &end, 10);
    if (end == value.c_str() + position || *end != '\0') return false;
    const long note = (octave + 1) * 12 + semitone;
    if (note < 0 || note > 127) return false;
    midi = static_cast<int>(note);
    return true;
}

bool parseFrequencyOrNote(const std::string& text, double& frequency,
                          double a4) {
    std::string value;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c)))
            value.push_back(c);
    }
    if (value.empty()) return false;
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.size() > 2 &&
        lower.substr(lower.size() - 2) == "hz")
        value.resize(value.size() - 2);
    int midi = 0;
    if (parseNoteName(value, midi)) {
        frequency = midiNoteFrequency(static_cast<double>(midi), a4);
        return std::isfinite(frequency) && frequency > 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed) ||
        parsed <= 0.0)
        return false;
    frequency = parsed;
    return true;
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
