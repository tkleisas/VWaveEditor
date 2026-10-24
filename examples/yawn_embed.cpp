#include "vwave/Document.h"
#include "vwave/Processing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    constexpr int sampleRate = 48000;
    constexpr int64_t frames = sampleRate;
    vwave::Audio audio(2, frames, sampleRate);
    for (int64_t frame = 0; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sampleRate;
        const double envelope = std::exp(-time * 2.0);
        audio.sample(0, frame) = static_cast<float>(
            0.5 * envelope * std::sin(2.0 * 3.14159265358979323846 * 220.0 * time));
        audio.sample(1, frame) = static_cast<float>(
            0.5 * envelope * std::sin(2.0 * 3.14159265358979323846 * 330.0 * time));
    }

    vwave::Document document = vwave::Document::fromAudio(std::move(audio));
    document.setSelection(0, frames / 2);
    document.normalize(-1.0);
    document.reverseSelection();
    document.setSelection(frames / 2, frames);
    document.fadeOut();
    document.timeStretch(1.5, vwave::StretchAlgorithm::PhaseVocoder);

    const vwave::Audio& rendered = document.audio();
    std::cout << "source frames: " << document.source().numFrames() << '\n';
    std::cout << "rendered frames: " << rendered.numFrames() << '\n';
    std::cout << "undo entries: " << document.undoCount() << '\n';
    std::cout << "undo label: " << document.undoLabel() << '\n';

    document.undo();
    std::cout << "after undo: " << document.audio().numFrames() << " frames\n";
    return 0;
}
