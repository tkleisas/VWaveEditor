#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

bool Document::gainDb(double db, std::string* error) {
    return apply(Operation::gain(db, activeRange()), error);
}

bool Document::normalize(double peakDb, std::string* error) {
    return apply(Operation::normalize(peakDb, activeRange()), error);
}

bool Document::fadeIn(std::string* error) {
    return apply(Operation::fadeIn(activeRange()), error);
}

bool Document::fadeOut(std::string* error) {
    return apply(Operation::fadeOut(activeRange()), error);
}

bool Document::reverseSelection(std::string* error) {
    return apply(Operation::reverse(activeRange()), error);
}

bool Document::invertSelection(std::string* error) {
    return apply(Operation::invert(activeRange()), error);
}

bool Document::silenceSelection(std::string* error) {
    return apply(Operation::silence(activeRange()), error);
}

bool Document::removeDc(std::string* error) {
    return apply(Operation::dcRemove(activeRange()), error);
}

bool Document::timeStretch(double ratio, StretchAlgorithm algorithm,
                           std::string* error) {
    return apply(Operation::timeStretch(ratio, activeRange(), algorithm), error);
}

bool Document::timeStretchTrack(double ratio, StretchAlgorithm algorithm,
                                std::string* error) {
    if (!m_cacheValid) rebuild();
    if (m_rendered.empty()) {
        setError(error, "document is empty");
        return false;
    }
    return apply(Operation::timeStretch(ratio, {0, m_rendered.numFrames()},
                                        algorithm),
                 error);
}

bool Document::pitchShift(double semitones, StretchAlgorithm algorithm,
                          std::string* error) {
    return apply(Operation::pitchShift(semitones, activeRange(), algorithm),
                 error);
}

bool Document::spectralGain(double lowHz, double highHz, double gainDb,
                            std::string* error) {
    return apply(
        Operation::spectralGain(lowHz, highHz, gainDb, activeRange()), error);
}

bool Document::spectralFilter(const std::string& type, double cutoffHz,
                              std::string* error) {
    return apply(Operation::spectralFilter(type, cutoffHz, activeRange()),
                 error);
}

bool Document::spectralGate(double thresholdDb, std::string* error) {
    return apply(Operation::spectralGate(thresholdDb, activeRange()), error);
}

bool Document::spectralBlur(int timeRadius, int frequencyRadius,
                            std::string* error) {
    return apply(
        Operation::spectralBlur(timeRadius, frequencyRadius, activeRange()),
        error);
}

bool Document::convolve(const Audio& impulseResponse, bool normalizeIr,
                        std::string* error) {
    return apply(Operation::convolve(impulseResponse, activeRange(), normalizeIr),
                 error);
}

bool Document::copySelection() {
    if (!m_cacheValid) rebuild();
    if (!m_selection.active || m_selection.empty()) return false;
    m_clipboard = audio().slice(m_selection.range());
    return true;
}

bool Document::cutSelection(std::string* error) {
    if (!m_cacheValid) rebuild();
    if (!m_selection.active || m_selection.empty()) return false;
    if (!copySelection()) return false;
    return deleteSelection(error);
}

bool Document::deleteSelection(std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::remove(m_selection.range()), error);
}

bool Document::moveSelectionTo(int64_t destination, bool copy,
                               std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::moveRange(m_selection.range(), destination, copy),
                 error);
}

bool Document::repeatSelection(int repeats, std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::repeatRange(m_selection.range(), repeats), error);
}

bool Document::paste(std::string* error) {
    if (m_clipboard.empty()) {
        setError(error, "clipboard is empty");
        return false;
    }
    if (m_selection.active && !m_selection.empty()) {
        return apply(Operation::replaceRange(m_selection.range(), m_clipboard),
                     error);
    }
    return apply(
        Operation::insert(m_rendered.numFrames(), m_clipboard), error);
}

bool Document::pasteAt(int64_t frame, std::string* error) {
    if (m_clipboard.empty()) {
        setError(error, "clipboard is empty");
        return false;
    }
    return apply(Operation::insert(frame, m_clipboard), error);
}

bool Document::cropToSelection(std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::crop(m_selection.range()), error);
}

bool Document::insertAudio(int64_t frame, Audio audio, std::string* error) {
    return apply(Operation::insert(frame, std::move(audio)), error);
}

bool Document::replaceSelection(Audio audio, std::string* error) {
    if (!m_selection.active || m_selection.empty()) {
        setError(error, "no active selection");
        return false;
    }
    return apply(Operation::replaceRange(m_selection.range(), std::move(audio)),
                 error);
}

bool Document::resample(int sampleRate, std::string* error) {
    return apply(Operation::resample(sampleRate), error);
}

} // namespace vwave
