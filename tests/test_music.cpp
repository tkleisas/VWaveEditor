#include "test_framework.h"

#include "vwave/Music.h"

#include <cmath>

using namespace vwave;

VWTEST("music note conversion identifies concert pitch") {
    CHECK_NEAR(midiNoteFrequency(69.0), 440.0, 1e-9);
    CHECK_NEAR(midiNoteFrequency(57.0), 220.0, 1e-9);
    CHECK_NEAR(frequencyToMidi(440.0), 69.0, 1e-9);
    CHECK_EQ(midiNoteName(69), std::string("A4"));
    CHECK_EQ(midiNoteName(60), std::string("C4"));

    const NoteInfo a4 = nearestNote(440.0);
    CHECK_EQ(a4.midi, 69);
    CHECK_EQ(a4.name, std::string("A4"));
    CHECK_NEAR(a4.cents, 0.0, 1e-9);

    const NoteInfo c4 = nearestNote(261.6255653005986);
    CHECK_EQ(c4.midi, 60);
    CHECK_NEAR(c4.cents, 0.0, 1e-6);

    const NoteInfo sharp = nearestNote(452.0);
    CHECK_EQ(sharp.midi, 69);
    CHECK_NEAR(sharp.cents, 46.58, 0.2);
}

VWTEST("music note conversion handles invalid frequencies") {
    const NoteInfo invalid = nearestNote(0.0);
    CHECK_EQ(invalid.midi, 60);
    CHECK_EQ(invalid.name, std::string("C4"));
    CHECK_NEAR(frequencyToMidi(-1.0), 0.0, 1e-9);
}
