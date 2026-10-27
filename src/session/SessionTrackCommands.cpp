#include "vwave/Session.h"

#include "CommandHelpers.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeTrackCommands(const std::string& command,
                                             const nlohmann::json& params,
                                             std::string* error) {
        if (command == "add_track") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            Audio audio;
            if (!readAudio(path, audio, error)) return nullptr;
            const double start = optionalDouble(params, "start", 0.0);
            if (!std::isfinite(start) || start < 0.0) {
                setError(error, "invalid track start time");
                return nullptr;
            }
            const int rate = m_timeline.empty() ? audio.sampleRate()
                                                : m_timeline.sampleRate();
            const int64_t startFrame =
                static_cast<int64_t>(std::llround(start * rate));
            std::string name = params.value("name", std::string{});
            if (name.empty()) {
                name = std::filesystem::path(path).stem().string();
                if (name.empty()) name = "Track " +
                                          std::to_string(
                                              m_timeline.trackCount() + 1);
            }
            if (!m_timeline.addAudioTrack(name, audio, startFrame, error))
                return nullptr;
            const int index = m_timeline.trackCount() - 1;
            m_timeline.setActiveTrack(index);
            m_timeline.track(index).document.setSourcePath(path);
            return info();
        }
        if (command == "add_empty_track") {
            const double start = optionalDouble(params, "start", 0.0);
            if (!std::isfinite(start) || start < 0.0) {
                setError(error, "invalid track start time");
                return nullptr;
            }
            const int rate = m_timeline.empty() ? 48000
                                                : m_timeline.sampleRate();
            const int64_t startFrame =
                static_cast<int64_t>(std::llround(start * rate));
            std::string name = params.value("name", std::string{});
            if (name.empty())
                name = "Track " +
                       std::to_string(m_timeline.trackCount() + 1);
            if (!m_timeline.addEmptyTrack(name, startFrame, error))
                return nullptr;
            m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
            return info();
        }
        if (command == "remove_track") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (!m_timeline.removeTrack(index)) {
                setError(error, "invalid track index");
                return nullptr;
            }
            return info();
        }
        if (command == "select_track") {
            if (!params.contains("index") || !params["index"].is_number()) {
                setError(error, "select_track requires an index");
                return nullptr;
            }
            const int index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            m_timeline.setActiveTrack(index);
            return info();
        }
        if (command == "track_set") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            if (params.contains("name"))
                m_timeline.setTrackName(index,
                                        params["name"].get<std::string>());
            if (params.contains("gain_db"))
                m_timeline.setTrackGainDb(index,
                                          params["gain_db"].get<double>());
            if (params.contains("pan"))
                m_timeline.setTrackPan(index, params["pan"].get<double>());
            if (params.contains("muted"))
                m_timeline.setTrackMuted(index,
                                         params["muted"].get<bool>());
            if (params.contains("solo"))
                m_timeline.setTrackSolo(index, params["solo"].get<bool>());
            if (params.contains("armed"))
                m_timeline.setTrackArmed(index, params["armed"].get<bool>());
            if (params.contains("start")) {
                const double start = params["start"].get<double>();
                if (!std::isfinite(start) || start < 0.0 ||
                    m_timeline.sampleRate() <= 0) {
                    setError(error, "invalid track start time");
                    return nullptr;
                }
                m_timeline.setTrackStartFrame(
                    index, static_cast<int64_t>(
                               std::llround(start * m_timeline.sampleRate())));
            }
            return info();
        }
        if (command == "selection_scope") {
            const std::string scope = params.value(
                "scope", std::string{"all_tracks"});
            if (scope == "all" || scope == "all_tracks") {
                m_timeline.setSelectionScope(SelectionScope::AllTracks);
            } else if (scope == "active" || scope == "active_track") {
                m_timeline.setSelectionScope(SelectionScope::ActiveTrack);
            } else {
                setError(error, "unknown selection scope: " + scope);
                return nullptr;
            }
            return {{"ok", true},
                    {"scope",
                     m_timeline.selectionScope() ==
                             SelectionScope::ActiveTrack
                         ? "active_track"
                         : "all_tracks"}};
        }

        if (command == "crop_track") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            Range range;
            bool hasRange = false;
            if (!parseOptionalRange(
                    m_timeline.track(index).document.audio(), params, range,
                    hasRange, error))
                return nullptr;
            if (hasRange) {
                range.start += m_timeline.track(index).startFrame;
                range.end += m_timeline.track(index).startFrame;
            } else {
                range = m_timeline.selectedRange();
            }
            if (!m_timeline.cropTrack(index, range, error)) return nullptr;
            return {{"ok", true},
                    {"index", index},
                    {"start_frame", m_timeline.track(index).startFrame},
                    {"frames",
                     m_timeline.track(index).document.numFrames()}};
        }

        if (command == "shift_track") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            if (params.contains("start") && params.contains("delta")) {
                setError(error, "use either start or delta, not both");
                return nullptr;
            }
            if (params.contains("start")) {
                const double start = params["start"].get<double>();
                if (!std::isfinite(start) || start < 0.0 ||
                    m_timeline.sampleRate() <= 0) {
                    setError(error, "invalid track start time");
                    return nullptr;
                }
                m_timeline.setTrackStartFrame(
                    index, static_cast<int64_t>(
                               std::llround(start * m_timeline.sampleRate())));
            } else if (params.contains("delta")) {
                const double delta = params["delta"].get<double>();
                if (!std::isfinite(delta) || m_timeline.sampleRate() <= 0) {
                    setError(error, "invalid track shift");
                    return nullptr;
                }
                if (!m_timeline.shiftTrack(
                        index, static_cast<int64_t>(std::llround(
                                   delta * m_timeline.sampleRate())),
                        error))
                    return nullptr;
            } else {
                setError(error, "missing start or delta");
                return nullptr;
            }
            return {{"ok", true},
                    {"index", index},
                    {"start_frame", m_timeline.track(index).startFrame}};
        }

    setError(error, "unknown track command: " + command);
    return nullptr;
}

} // namespace vwave
