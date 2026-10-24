#include "vwave/Synth.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vwave {

namespace {

double envelopeValue(const Adsr& envelope, double time, double noteOff) {
    const double attack = std::max(0.0, envelope.attack);
    const double decay = std::max(0.0, envelope.decay);
    const double sustain = std::clamp(envelope.sustain, 0.0, 1.0);
    const double release = std::max(0.0, envelope.release);

    double value = 0.0;
    if (attack > 0.0 && time < attack) {
        value = time / attack;
    } else if (decay > 0.0 && time < attack + decay) {
        const double position = (time - attack) / decay;
        value = 1.0 + (sustain - 1.0) * position;
    } else {
        value = sustain;
    }

    if (time >= noteOff) {
        if (release > 0.0) {
            const double releasePosition = (time - noteOff) / release;
            value = sustain * std::max(0.0, 1.0 - releasePosition);
        } else {
            value = 0.0;
        }
    } else if (noteOff < attack && attack > 0.0) {
        value = noteOff / attack * std::max(0.0, 1.0 - time / noteOff);
    }
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

Audio renderWavetableNote(const Wavetable& wavetable, double midiNote,
                          double durationSeconds,
                          const SynthNoteOptions& options) {
    if (!wavetable.valid() || !std::isfinite(midiNote) ||
        !std::isfinite(durationSeconds) || durationSeconds <= 0.0)
        return Audio{};
    if (options.sampleRate < 0 || options.channels < 1 ||
        options.channels > 2)
        return Audio{};

    const int sampleRate = options.sampleRate > 0
                               ? options.sampleRate
                               : (wavetable.sampleRate() > 0
                                      ? wavetable.sampleRate()
                                      : 48000);
    if (sampleRate <= 0) return Audio{};
    const int64_t frames = static_cast<int64_t>(
        std::llround(durationSeconds * static_cast<double>(sampleRate)));
    if (frames <= 0) return Audio{};

    const double rootNote = std::isfinite(options.rootNote)
                                ? options.rootNote
                                : 69.0;
    const double frequency =
        440.0 * std::pow(2.0, (midiNote - rootNote) / 12.0);
    if (!std::isfinite(frequency) || frequency <= 0.0) return Audio{};

    const int unison = std::clamp(options.unison, 1, 16);
    const double detune = std::isfinite(options.detuneCents)
                              ? options.detuneCents
                              : 0.0;
    const double gain = std::isfinite(options.gain) ? options.gain : 0.8;
    const double morph = std::clamp(options.morph, 0.0, 1.0);
    const double framePosition =
        morph * static_cast<double>(wavetable.frameCount() - 1);
    const double release = std::max(0.0, options.envelope.release);
    const double noteOff =
        std::max(0.0, durationSeconds - std::min(release, durationSeconds));

    Audio output(options.channels, frames, sampleRate);
    std::vector<double> phases(static_cast<size_t>(unison), 0.0);
    std::vector<double> increments(static_cast<size_t>(unison), 0.0);
    std::vector<double> leftGain(static_cast<size_t>(unison), 1.0);
    std::vector<double> rightGain(static_cast<size_t>(unison), 1.0);
    for (int voice = 0; voice < unison; ++voice) {
        const double spread = unison > 1
                                  ? static_cast<double>(voice) /
                                            static_cast<double>(unison - 1) -
                                        0.5
                                  : 0.0;
        const double cents = detune * spread * 2.0;
        const double voiceFrequency =
            frequency * std::pow(2.0, cents / 1200.0);
        increments[static_cast<size_t>(voice)] =
            voiceFrequency / static_cast<double>(sampleRate);
        phases[static_cast<size_t>(voice)] =
            static_cast<double>(voice) * 0.37 / static_cast<double>(unison);
        const double pan = unison > 1
                               ? 0.15 + 0.7 * static_cast<double>(voice) /
                                            static_cast<double>(unison - 1)
                               : 0.5;
        const double angle = pan * 3.14159265358979323846 / 2.0;
        leftGain[static_cast<size_t>(voice)] = std::cos(angle);
        rightGain[static_cast<size_t>(voice)] = std::sin(angle);
    }

    const double voiceScale = gain / std::sqrt(static_cast<double>(unison));
    for (int64_t frame = 0; frame < frames; ++frame) {
        const double time =
            static_cast<double>(frame) / static_cast<double>(sampleRate);
        const double envelope =
            envelopeValue(options.envelope, time, noteOff);
        if (envelope <= 0.0) continue;
        double left = 0.0;
        double right = 0.0;
        for (int voice = 0; voice < unison; ++voice) {
            const double value = wavetable.sample(
                framePosition, phases[static_cast<size_t>(voice)]);
            left += value * leftGain[static_cast<size_t>(voice)];
            right += value * rightGain[static_cast<size_t>(voice)];
            double& phase = phases[static_cast<size_t>(voice)];
            phase += increments[static_cast<size_t>(voice)];
            phase -= std::floor(phase);
        }
        const double amplitude = voiceScale * envelope;
        if (options.channels == 1) {
            output.sample(0, frame) = static_cast<float>(
                (left + right) * 0.7071067811865476 * amplitude);
        } else {
            output.sample(0, frame) =
                static_cast<float>(left * amplitude * 1.4142135623730951);
            output.sample(1, frame) =
                static_cast<float>(right * amplitude * 1.4142135623730951);
        }
    }
    return output;
}

} // namespace vwave
