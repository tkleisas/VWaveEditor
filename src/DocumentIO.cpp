#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <fstream>
#include <string>
#include <utility>

using namespace vwave::detail;

namespace vwave {

bool Document::loadAudio(const std::string& path, std::string* error) {
    if (error) error->clear();
    Audio loaded;
    if (!readAudio(path, loaded, error)) return false;
    if (loaded.empty()) {
        setError(error, "audio file is empty");
        return false;
    }
    m_source = std::move(loaded);
    m_sourcePath = path;
    m_history.clear();
    m_redo.clear();
    m_rendered = m_source;
    m_arranged = m_rendered;
    m_clips.clear();
    m_cacheValid = true;
    m_arrangedValid = true;
    m_selection.clear();
    m_clipboard = Audio{};
    m_markers.clear();
    m_slices.clear();
    ++m_revision;
    return true;
}

bool Document::saveAudio(const std::string& path,
                         const AudioExportOptions& options,
                         std::string* error) const {
    if (error) error->clear();
    if (audio().empty()) {
        setError(error, "document is empty");
        return false;
    }
    return writeAudio(path, audio(), options, error);
}

nlohmann::json Document::toJson() const {
    if (m_source.empty())
        throw std::runtime_error("document has no source audio");
    nlohmann::json root;
    root["format"] = "vwave-project";
    root["version"] = 1;
    root["source"] = audioToJson(m_source);
    root["sourcePath"] = m_sourcePath;
    root["selection"] = selectionToJson(m_selection);
    root["markers"] = nlohmann::json::array();
    for (const auto& marker : m_markers)
        root["markers"].push_back(
            {{"frame", marker.frame}, {"name", marker.name}});
    root["slices"] = nlohmann::json::array();
    for (const auto& slice : m_slices)
        root["slices"].push_back(
            {{"start", slice.range.start},
             {"end", slice.range.end},
             {"name", slice.name}});
    root["history"] = nlohmann::json::array();
    for (const auto& entry : m_history) {
        root["history"].push_back(
            {{"operation", entry.operation.toJson()},
             {"before", selectionToJson(entry.before)},
             {"after", selectionToJson(entry.after)}});
    }
    return root;
}

bool Document::fromJson(const nlohmann::json& root, Document& out,
                        std::string* error) {
    if (error) error->clear();
    try {
        if (root.value("format", std::string{}) != "vwave-project") {
            setError(error, "not a VWaveEditor project");
            return false;
        }
        if (root.value("version", 0) != 1) {
            setError(error, "unsupported project version");
            return false;
        }

        Audio source;
        if (!audioFromJson(root.at("source"), source, error)) return false;
        if (source.empty()) {
            setError(error, "project source audio is empty");
            return false;
        }

        Document loaded(std::move(source));
        loaded.m_sourcePath = root.value("sourcePath", std::string{});
        loaded.m_selection = selectionFromJson(
            root.value("selection", nlohmann::json{}));

        if (root.contains("markers") && root["markers"].is_array()) {
            for (const auto& marker : root["markers"]) {
                const int64_t frame = marker.value("frame", int64_t{0});
                if (frame < 0) continue;
                loaded.m_markers.push_back(
                    {frame, marker.value("name", std::string{})});
            }
            std::stable_sort(
                loaded.m_markers.begin(), loaded.m_markers.end(),
                [](const Marker& a, const Marker& b) {
                    return a.frame < b.frame;
                });
        }
        if (root.contains("slices") && root["slices"].is_array()) {
            for (const auto& slice : root["slices"]) {
                const int64_t start = slice.value("start", int64_t{0});
                const int64_t end = slice.value("end", int64_t{0});
                if (start < 0 || end <= start) continue;
                loaded.m_slices.push_back(
                    {{start, end}, slice.value("name", std::string{})});
            }
        }

        if (root.contains("history") && root["history"].is_array()) {
            Audio working = loaded.m_source;
            std::vector<Clip> clips;
            for (const auto& item : root["history"]) {
                Operation operation;
                if (!Operation::fromJson(item.at("operation"), operation,
                                         error))
                    return false;
                loaded.m_rendered = working;
                loaded.m_clips.assign(clips);
                loaded.m_cacheValid = true;
                if (!loaded.validateRange(operation, error)) return false;

                Selection after = selectionFromJson(
                    item.value("after", nlohmann::json{}));
                if (!loaded.applyHistoryOperation(operation, working, clips,
                                                  after, error)) {
                    setError(error, "cannot replay project operation: " +
                                        operation.label + ": " +
                                        (error ? *error : std::string{}));
                    return false;
                }
                Selection before = selectionFromJson(
                    item.value("before", nlohmann::json{}));
                loaded.m_history.push_back(
                    {std::move(operation), before, after});
            }
            loaded.m_rendered = std::move(working);
            loaded.m_clips.assign(std::move(clips));
            loaded.m_arranged = loaded.m_clips.render(loaded.m_rendered);
            loaded.m_arrangedValid = true;
            loaded.m_cacheValid = true;
            loaded.m_redo.clear();
        } else {
            loaded.m_rendered = loaded.m_source;
            loaded.m_arranged = loaded.m_rendered;
            loaded.m_cacheValid = true;
            loaded.m_arrangedValid = true;
        }

        const int64_t arrangedFrames = loaded.m_arranged.numFrames();
        loaded.m_markers.erase(
            std::remove_if(loaded.m_markers.begin(), loaded.m_markers.end(),
                           [arrangedFrames](const Marker& marker) {
                               return marker.frame > arrangedFrames;
                           }),
            loaded.m_markers.end());
        loaded.m_slices.erase(
            std::remove_if(loaded.m_slices.begin(), loaded.m_slices.end(),
                           [arrangedFrames](const Slice& slice) {
                               return slice.range.end > arrangedFrames;
                           }),
            loaded.m_slices.end());

        ++loaded.m_revision;
        out = std::move(loaded);
        out.m_selection =
            clampSelection(out.m_selection, out.m_arranged.numFrames());
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

bool Document::saveProject(const std::string& path, std::string* error) const {
    if (error) error->clear();
    if (m_source.empty()) {
        setError(error, "document has no source audio");
        return false;
    }
    try {
        const nlohmann::json root = toJson();
        std::ofstream outFile(path, std::ios::binary);
        if (!outFile) {
            setError(error, "cannot create project file");
            return false;
        }
        outFile << root.dump(2) << '\n';
        if (!outFile.good()) {
            setError(error, "failed while writing project file");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot save project: ") + e.what());
        return false;
    }
}

bool Document::loadProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            setError(error, "cannot open project file");
            return false;
        }
        nlohmann::json root;
        in >> root;
        return fromJson(root, *this, error);
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load project: ") + e.what());
        return false;
    }
}

} // namespace vwave
