#include "vwave/Session.h"

#include "CommandHelpers.h"

#include <cmath>
#include <string>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeClipCommands(const std::string& command,
                                            const nlohmann::json& params,
                                            std::string* error) {

            if (m_timeline.empty()) {
                setError(error, "no active track");
                return nullptr;
            }
            int trackIndex = m_timeline.activeTrack();
            if (params.contains("track"))
                trackIndex = params["track"].get<int>();
            else if (params.contains("track_index"))
                trackIndex = params["track_index"].get<int>();
            if (trackIndex < 0 || trackIndex >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            Document& clipDocument =
                m_timeline.track(trackIndex).document;

            if (command == "make_clips") {
                if (!clipDocument.createClipArrangement(error)) return nullptr;
            } else if (command == "flatten_clips") {
                if (!clipDocument.flattenClips(error)) return nullptr;
            } else {
                int clipIndex = -1;
                if (params.contains("clip"))
                    clipIndex = params["clip"].get<int>();
                else if (params.contains("clip_index"))
                    clipIndex = params["clip_index"].get<int>();
                if (command == "split_clip") {
                    const double at = params.value("at", 0.0);
                    if (!std::isfinite(at) || at < 0.0 ||
                        clipDocument.sampleRate() <= 0) {
                        setError(error, "invalid clip split position");
                        return nullptr;
                    }
                    const int64_t frame = static_cast<int64_t>(
                        std::llround(at * clipDocument.sampleRate()));
                    if (!clipDocument.splitClipAt(frame, error))
                        return nullptr;
                } else {
                    if (clipIndex < 0) {
                        setError(error, "clip index is required");
                        return nullptr;
                    }
                    const size_t index = static_cast<size_t>(clipIndex);
                    if (command == "delete_clip") {
                        if (!clipDocument.deleteClip(index, error))
                            return nullptr;
                    } else if (command == "move_clip") {
                        const double at = params.value("at", 0.0);
                        if (!std::isfinite(at) || at < 0.0 ||
                            clipDocument.sampleRate() <= 0) {
                            setError(error, "invalid clip position");
                            return nullptr;
                        }
                        const int64_t frame = static_cast<int64_t>(
                            std::llround(at * clipDocument.sampleRate()));
                        if (!clipDocument.moveClip(index, frame, error))
                            return nullptr;
                    } else if (command == "stretch_clip") {
                        if (!clipDocument.stretchClip(
                                index, optionalDouble(params, "ratio", 1.0),
                                error))
                            return nullptr;
                    } else if (command == "repeat_clip") {
                        if (!clipDocument.repeatClip(
                                index, optionalInt(params, "count", 1), error))
                            return nullptr;
                    } else if (command == "set_clip_gain") {
                        if (!clipDocument.setClipGain(
                                index, optionalDouble(params, "gain_db", 0.0),
                                error))
                            return nullptr;
                    } else {
                        if (!clipDocument.setClipFades(
                                index,
                                optionalDouble(params, "fade_in", 0.0),
                                optionalDouble(params, "fade_out", 0.0), error))
                            return nullptr;
                    }
                }
            }
            if (trackIndex == m_timeline.activeTrack())
                syncSelectionFromActive();
            return info();
        }

} // namespace vwave
