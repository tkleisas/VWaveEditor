#include "vwave/Timeline.h"

#include "TimelineInternal.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

nlohmann::json Timeline::toJson() const {
    nlohmann::json root;
    root["format"] = "vwave-timeline";
    root["version"] = 1;
    root["sampleRate"] = m_sampleRate;
    root["activeTrack"] = m_active;
    root["selectionScope"] =
        m_selectionScope == SelectionScope::ActiveTrack ? "active_track"
                                                         : "all_tracks";
    root["selection"] = selectionJson(m_selection);
    root["tracks"] = nlohmann::json::array();
    for (const auto& item : m_tracks) {
        nlohmann::json trackJson;
        trackJson["name"] = item.name;
        if (!item.recipe.is_null() && !item.recipe.empty())
            trackJson["recipe"] = item.recipe;
        trackJson["startFrame"] = item.startFrame;
        trackJson["gainDb"] = item.gainDb;
        trackJson["pan"] = item.pan;
        trackJson["muted"] = item.muted;
        trackJson["solo"] = item.solo;
        trackJson["armed"] = item.armed;
        trackJson["empty"] = item.empty;
        if (!item.empty)
            trackJson["document"] = item.document.toJson();
        else
            trackJson["sampleRate"] = item.document.sampleRate();
        root["tracks"].push_back(std::move(trackJson));
    }
    return root;
}

bool Timeline::saveProject(const std::string& path,
                           std::string* error) const {
    if (error) error->clear();
    if (m_tracks.empty()) {
        setError(error, "project has no tracks");
        return false;
    }
    try {
        const nlohmann::json root = toJson();
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            setError(error, "cannot create project file");
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            setError(error, "failed while writing project file");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot save project: ") + e.what());
        return false;
    }
}

bool Timeline::loadProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            setError(error, "cannot open project file");
            return false;
        }
        nlohmann::json root;
        in >> root;

        if (root.value("format", std::string{}) == "vwave-project") {
            Document document;
            if (!Document::fromJson(root, document, error)) return false;
            Timeline timeline;
            std::string name = "Track 1";
            if (!document.sourcePath().empty())
                name = std::filesystem::path(document.sourcePath())
                           .stem()
                           .string();
            const Selection savedSelection = document.selection();
            if (!timeline.addTrack(name, std::move(document), 0, error))
                return false;
            timeline.setSelection(savedSelection.start, savedSelection.end);
            *this = std::move(timeline);
            return true;
        }

        if (root.value("format", std::string{}) != "vwave-timeline") {
            setError(error, "not a VWaveEditor project");
            return false;
        }
        if (root.value("version", 0) != 1) {
            setError(error, "unsupported project version");
            return false;
        }
        const int sampleRate = root.value("sampleRate", 0);
        if (sampleRate <= 0) {
            setError(error, "project has an invalid sample rate");
            return false;
        }
        if (!root.contains("tracks") || !root["tracks"].is_array() ||
            root["tracks"].empty()) {
            setError(error, "project has no tracks");
            return false;
        }

        Timeline loaded;
        loaded.m_sampleRate = sampleRate;
        for (const auto& trackJson : root["tracks"]) {
            TimelineTrack item;
            item.name = trackJson.value("name", std::string{});
            if (trackJson.contains("recipe") &&
                trackJson["recipe"].is_object())
                item.recipe = trackJson["recipe"];
            item.startFrame = trackJson.value("startFrame", int64_t{0});
            item.gainDb = trackJson.value("gainDb", 0.0);
            item.pan = trackJson.value("pan", 0.0);
            item.muted = trackJson.value("muted", false);
            item.solo = trackJson.value("solo", false);
            item.armed = trackJson.value("armed", false);
            item.empty = trackJson.value("empty", false);
            if (item.startFrame < 0 || !std::isfinite(item.gainDb) ||
                !std::isfinite(item.pan)) {
                setError(error, "invalid track metadata");
                return false;
            }
            item.gainDb = std::clamp(item.gainDb, -96.0, 24.0);
            item.pan = std::clamp(item.pan, -1.0, 1.0);
            if (item.empty) {
                const int trackRate =
                    trackJson.value("sampleRate", sampleRate);
                item.document = Document::fromAudio(
                    Audio(1, 0, trackRate));
                item.empty = true;
            } else {
                if (!trackJson.contains("document")) {
                    setError(error, "track is missing its document");
                    return false;
                }
                if (!Document::fromJson(trackJson["document"],
                                        item.document, error))
                    return false;
                if (item.document.sampleRate() != sampleRate) {
                    setError(error, "track sample rate does not match project");
                    return false;
                }
                item.empty = item.document.source().empty();
            }
            loaded.m_tracks.push_back(std::move(item));
        }

        loaded.m_active = root.value("activeTrack", 0);
        if (loaded.m_tracks.empty()) loaded.m_active = -1;
        else loaded.m_active = std::clamp(
            loaded.m_active, 0, static_cast<int>(loaded.m_tracks.size()) - 1);
        loaded.m_selectionScope =
            root.value("selectionScope", std::string{"all_tracks"}) ==
                    "active_track"
                ? SelectionScope::ActiveTrack
                : SelectionScope::AllTracks;
        loaded.m_selection = selectionFromJson(
            root.value("selection", nlohmann::json{}));
        loaded.setSelection(loaded.m_selection.start, loaded.m_selection.end);
        *this = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

} // namespace vwave
