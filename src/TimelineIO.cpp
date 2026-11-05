#include "vwave/Timeline.h"

#include "TimelineInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace vwave::detail;

namespace vwave {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool safeDirectoryName(const std::string& name) {
    if (name.empty()) return false;
    const std::filesystem::path path(name);
    return !path.has_parent_path() && path.filename() == path;
}

std::string workAreaGeneration() {
    static std::atomic<unsigned long long> counter{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(stamp) + "-" +
           std::to_string(counter.fetch_add(1));
}

std::string trackDirectoryName(int index, const std::string& generation) {
    return "track-" + std::to_string(index) + "-" + generation;
}

bool writeJsonFile(const std::filesystem::path& path,
                   const nlohmann::json& root, std::string* error) {
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            setError(error, "cannot create project file");
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            setError(error, "failed while writing project file");
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        setError(error, "cannot replace project file");
        return false;
    }
    return true;
}

} // namespace

nlohmann::json Timeline::projectJson(bool includeBlockAudio) const {
    nlohmann::json root;
    root["format"] = "vwave-timeline";
    root["version"] = 2;
    root["sampleRate"] = m_sampleRate;
    root["activeTrack"] = m_active;
    root["selectionScope"] =
        m_selectionScope == SelectionScope::ActiveTrack ? "active_track"
                                                         : "all_tracks";
    root["selection"] = selectionJson(m_selection);
    root["tracks"] = nlohmann::json::array();
    for (size_t index = 0; index < m_tracks.size(); ++index) {
        const auto& item = m_tracks[index];
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
        if (!item.empty) {
            trackJson["document"] = item.document.toJson(includeBlockAudio);
        } else {
            trackJson["sampleRate"] = item.document.sampleRate();
        }
        root["tracks"].push_back(std::move(trackJson));
    }
    return root;
}

nlohmann::json Timeline::toJson() const { return projectJson(true); }

bool Timeline::saveProject(const std::string& path,
                           std::string* error) const {
    if (error) error->clear();
    if (m_tracks.empty()) {
        setError(error, "project has no tracks");
        return false;
    }
    try {
        const std::filesystem::path projectPath(path);
        const std::string workAreaName =
            projectPath.filename().string() + ".blocks";
        const std::filesystem::path workArea =
            projectPath.parent_path() / workAreaName;
        std::error_code ec;
        std::filesystem::create_directories(workArea, ec);
        if (ec) {
            setError(error, "cannot create project work area");
            return false;
        }

        const std::string generation = workAreaGeneration();
        std::vector<std::string> blockDirectories(m_tracks.size());
        for (size_t index = 0; index < m_tracks.size(); ++index) {
            const auto& item = m_tracks[index];
            if (item.empty) continue;
            const std::string name =
                trackDirectoryName(static_cast<int>(index), generation);
            const auto directory = workArea / name;
            if (!item.document.writeBlockDirectory(directory, error))
                return false;
            blockDirectories[index] = name;
        }

        nlohmann::json root = projectJson(false);
        root["workArea"] = workAreaName;
        for (size_t index = 0; index < m_tracks.size(); ++index) {
            if (!blockDirectories[index].empty())
                root["tracks"][index]["blockDirectory"] =
                    blockDirectories[index];
        }
        if (!writeJsonFile(projectPath, root, error)) return false;
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
            clearUndoHistory();
            return true;
        }

        if (root.value("format", std::string{}) != "vwave-timeline") {
            setError(error, "not a VWaveEditor project");
            return false;
        }
        const int version = root.value("version", 0);
        if (version != 1 && version != 2) {
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

        std::filesystem::path workArea;
        if (version >= 2) {
            std::string workAreaName =
                root.value("workArea", std::string{});
            if (workAreaName.empty())
                workAreaName =
                    std::filesystem::path(path).filename().string() +
                    ".blocks";
            if (!safeDirectoryName(workAreaName)) {
                setError(error, "project work area has an invalid name");
                return false;
            }
            workArea = std::filesystem::path(path).parent_path() /
                       workAreaName;
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
                if (version >= 2 && trackJson.contains("blockDirectory")) {
                    const std::string blockDirectory =
                        trackJson["blockDirectory"].get<std::string>();
                    if (!safeDirectoryName(blockDirectory)) {
                        setError(error, "track work area has an invalid name");
                        return false;
                    }
                    AudioBlockStore store;
                    if (!store.load(workArea / blockDirectory, error))
                        return false;
                    if (!Document::fromJson(trackJson["document"],
                                            item.document, error, &store))
                        return false;
                    item.document.setBlockCacheLimitBytes(
                        256ull * 1024ull * 1024ull);
                } else {
                    if (!Document::fromJson(trackJson["document"],
                                            item.document, error))
                        return false;
                }
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

        if (version >= 2 && !workArea.empty()) {
            std::set<std::filesystem::path> keep;
            for (const auto& trackJson : root["tracks"]) {
                if (!trackJson.contains("blockDirectory")) continue;
                const std::string name =
                    trackJson["blockDirectory"].get<std::string>();
                keep.insert(workArea / name);
            }
            std::error_code iteratorError;
            for (const auto& entry :
                 std::filesystem::directory_iterator(workArea,
                                                     iteratorError)) {
                if (!entry.is_directory()) continue;
                const std::string name = entry.path().filename().string();
                if (name.rfind("track-", 0) != 0) continue;
                if (keep.find(entry.path()) == keep.end())
                    std::filesystem::remove_all(entry.path(), iteratorError);
                iteratorError.clear();
            }
        }

        *this = std::move(loaded);
        clearUndoHistory();
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

} // namespace vwave
