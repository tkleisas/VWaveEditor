#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Processing.h"

#include <cmath>
#include <string>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeEditCommands(const std::string& command,
                                            const nlohmann::json& params,
                                            std::string* error) {
        if (command == "select") {
            if (!params.contains("start") || !params.contains("end")) {
                setError(error, "select requires start and end");
                return nullptr;
            }
            Range range;
            bool hasRange = false;
            if (!parseOptionalRange(document().audio(), params, range,
                                    hasRange, error))
                return nullptr;
            if (!hasRange || range.empty()) {
                setError(error, "selection is empty");
                return nullptr;
            }
            document().setSelection(range.start, range.end);
            syncSelectionFromActive();
            return {{"ok", true}, {"selection", info()["selection"]}};
        }
        if (command == "select_all") {
            document().selectAll();
            syncSelectionFromActive();
            return {{"ok", true}, {"selection", info()["selection"]}};
        }
        if (command == "clear_selection") {
            document().clearSelection();
            m_timeline.clearSelection();
            return okResult();
        }

        if (command == "undo") {
            if (!document().undo()) {
                setError(error, "nothing to undo");
                return nullptr;
            }
            return info();
        }
        if (command == "redo") {
            if (!document().redo()) {
                setError(error, "nothing to redo");
                return nullptr;
            }
            return info();
        }

        if (command == "copy") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().copySelection()) {
                setError(error, "cannot copy selection");
                return nullptr;
            }
            return {{"ok", true},
                    {"clipboard_frames", document().clipboard().numFrames()}};
        }
        if (command == "cut") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().cutSelection(error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "delete") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().deleteSelection(error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "paste") {
            if (params.contains("at")) {
                const double at = params["at"].get<double>();
                if (!std::isfinite(at) || at < 0.0) {
                    setError(error, "invalid paste position");
                    return nullptr;
                }
                const int64_t frame = static_cast<int64_t>(
                    std::llround(at * document().sampleRate()));
                if (!document().pasteAt(frame, error)) return nullptr;
            } else {
                if (!document().paste(error)) return nullptr;
            }
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "crop") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!document().cropToSelection(error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "move_selection") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (!params.contains("at") && !params.contains("destination")) {
                setError(error, "move_selection requires at");
                return nullptr;
            }
            const double at = params.contains("at")
                                  ? params["at"].get<double>()
                                  : params["destination"].get<double>();
            if (!std::isfinite(at) || at < 0.0) {
                setError(error, "invalid move destination");
                return nullptr;
            }
            const int64_t frame = static_cast<int64_t>(
                std::llround(at * document().sampleRate()));
            const bool copy = optionalBool(params, "copy", false);
            if (!document().moveSelectionTo(frame, copy, error))
                return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "repeat_selection") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            const int repeats = optionalInt(params, "repeats",
                                             optionalInt(params, "count", 1));
            if (!document().repeatSelection(repeats, error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "snap_selection") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            const auto selection = document().selection();
            if (!selection.active) {
                setError(error, "no active selection");
                return nullptr;
            }
            const double radiusMs = optionalDouble(params, "radius_ms", 10.0);
            if (!std::isfinite(radiusMs) || radiusMs < 0.0) {
                setError(error, "invalid snap radius");
                return nullptr;
            }
            const int64_t radius = static_cast<int64_t>(std::llround(
                radiusMs * document().sampleRate() / 1000.0));
            const Range snapped = snapRangeToZeroCrossings(
                document().audio(), selection.range(), radius);
            document().setSelection(snapped.start, snapped.end);
            syncSelectionFromActive();
            return {{"ok", true},
                    {"start", snapped.start},
                    {"end", snapped.end},
                    {"start_seconds",
                     static_cast<double>(snapped.start) /
                         document().sampleRate()},
                    {"end_seconds",
                     static_cast<double>(snapped.end) /
                         document().sampleRate()}};
        }
        if (command == "gain" || command == "normalize" ||
            command == "fade_in" || command == "fade_out" ||
            command == "reverse" || command == "invert" ||
            command == "silence" || command == "dc_remove") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            bool success = false;
            if (command == "gain")
                success = document().gainDb(optionalDouble(params, "db", 0.0),
                                            error);
            else if (command == "normalize")
                success = document().normalize(
                    optionalDouble(params, "peak_db", -1.0), error);
            else if (command == "fade_in")
                success = document().fadeIn(error);
            else if (command == "fade_out")
                success = document().fadeOut(error);
            else if (command == "reverse")
                success = document().reverseSelection(error);
            else if (command == "invert")
                success = document().invertSelection(error);
            else if (command == "silence")
                success = document().silenceSelection(error);
            else
                success = document().removeDc(error);
            if (!success) return nullptr;
            return okResult();
        }

        if (command == "insert_silence") {
            const double seconds = params.value("seconds", 0.0);
            const double at = params.value("at", 0.0);
            if (!std::isfinite(seconds) || seconds <= 0.0 ||
                !std::isfinite(at) || at < 0.0) {
                setError(error, "invalid silence parameters");
                return nullptr;
            }
            const int64_t frames = static_cast<int64_t>(
                std::llround(seconds * document().sampleRate()));
            const int64_t position = static_cast<int64_t>(
                std::llround(at * document().sampleRate()));
            Audio silence(document().numChannels(), frames,
                          document().sampleRate());
            if (!document().insertAudio(position, std::move(silence), error))
                return nullptr;
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "insert_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            Audio audio;
            if (!readAudio(path, audio, error)) return nullptr;
            if (audio.numChannels() != document().numChannels()) {
                setError(error, "inserted audio channel count does not match");
                return nullptr;
            }
            const double at = optionalDouble(params, "at", 0.0);
            if (!std::isfinite(at) || at < 0.0) {
                setError(error, "invalid insert position");
                return nullptr;
            }
            const int64_t position = static_cast<int64_t>(
                std::llround(at * document().sampleRate()));
            if (!document().insertAudio(position, std::move(audio), error))
                return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

        if (command == "time_stretch" || command == "paulstretch" ||
            command == "pitch_shift") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            if (command == "time_stretch") {
                const double ratio = params.value("ratio", 1.0);
                const StretchAlgorithm algorithm =
                    stretchAlgorithm(params, error);
                if (error && !error->empty()) return nullptr;
                if (!document().timeStretch(ratio, algorithm, error))
                    return nullptr;
            } else if (command == "paulstretch") {
                const double ratio = params.value("ratio", 1.0);
                if (!document().timeStretch(ratio, StretchAlgorithm::PaulStretch,
                                            error))
                    return nullptr;
            } else {
                const double semitones = params.value("semitones", 0.0);
                const StretchAlgorithm algorithm =
                    stretchAlgorithm(params, error);
                if (error && !error->empty()) return nullptr;
                if (!document().pitchShift(semitones, algorithm, error))
                    return nullptr;
            }
            syncSelectionFromActive();
            return okResult();
        }
        if (command == "resample") {
            const int sampleRate = params.value("sample_rate", 0);
            if (!document().resample(sampleRate, error)) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

        if (command == "spectral_gain" || command == "spectral_filter" ||
            command == "spectral_gate" || command == "spectral_blur") {
            if (!prepareSelection(m_timeline, params, error)) return nullptr;
            bool success = false;
            if (command == "spectral_gain") {
                success = document().spectralGain(
                    optionalDouble(params, "low_hz", 0.0),
                    optionalDouble(params, "high_hz", 0.0),
                    optionalDouble(params, "db", 0.0), error);
            } else if (command == "spectral_filter") {
                success = document().spectralFilter(
                    params.value("type", std::string{}),
                    optionalDouble(params, "cutoff_hz", 0.0), error);
            } else if (command == "spectral_gate") {
                success = document().spectralGate(
                    optionalDouble(params, "threshold_db", -80.0), error);
            } else {
                success = document().spectralBlur(
                    optionalInt(params, "time_radius", 1),
                    optionalInt(params, "frequency_radius", 2), error);
            }
            if (!success) return nullptr;
            syncSelectionFromActive();
            return okResult();
        }

        if (command == "add_marker") {
            const double time = params.value("time", -1.0);
            if (!std::isfinite(time) || time < 0.0 ||
                time > document().duration()) {
                setError(error, "invalid marker time");
                return nullptr;
            }
            const int64_t frame = static_cast<int64_t>(
                std::llround(time * document().sampleRate()));
            if (!document().addMarker(
                    frame, params.value("name", std::string{}))) {
                setError(error, "cannot add marker");
                return nullptr;
            }
            return okResult();
        }
        if (command == "remove_marker") {
            const int index = params.value("index", -1);
            if (index < 0 ||
                !document().removeMarker(static_cast<size_t>(index))) {
                setError(error, "invalid marker index");
                return nullptr;
            }
            return okResult();
        }
        if (command == "clear_markers") {
            document().clearMarkers();
            return okResult();
        }

    setError(error, "unknown edit command: " + command);
    return nullptr;
}

} // namespace vwave
