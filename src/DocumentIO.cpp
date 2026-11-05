#include "vwave/Document.h"

#include "DocumentInternal.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace vwave::detail;

namespace vwave {

namespace {

nlohmann::json regionsToJson(const std::vector<AudioRegion>& regions) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& region : regions)
        array.push_back(audioRegionToJson(region));
    return array;
}

bool regionsFromJson(const nlohmann::json& json,
                     std::vector<AudioRegion>& regions,
                     std::string* error) {
    regions.clear();
    if (!json.is_array()) {
        setError(error, "audio regions must be an array");
        return false;
    }
    for (const auto& value : json) {
        AudioRegion region;
        if (!audioRegionFromJson(value, region, error)) return false;
        regions.push_back(std::move(region));
    }
    return true;
}

void collectBlockIds(const std::vector<AudioRegion>& regions,
                     std::set<AudioBlockId>& ids) {
    for (const auto& region : regions) {
        if (region.block != kInvalidAudioBlock) ids.insert(region.block);
    }
}

} // namespace

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
    if (!initializeSource()) {
        setError(error, "cannot initialize audio document");
        return false;
    }
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

std::set<AudioBlockId> Document::referencedBlockIds() const {
    std::set<AudioBlockId> blockIds;
    if (m_sourceBlock != kInvalidAudioBlock) blockIds.insert(m_sourceBlock);
    collectBlockIds(m_arrangement.regions(), blockIds);
    for (const auto& entry : m_history) {
        collectBlockIds(entry.before, blockIds);
        collectBlockIds(entry.after, blockIds);
    }
    for (const auto& entry : m_redo) {
        collectBlockIds(entry.before, blockIds);
        collectBlockIds(entry.after, blockIds);
    }
    return blockIds;
}

bool Document::writeBlockDirectory(const std::filesystem::path& directory,
                                   std::string* error) const {
    if (!m_store.save(directory, referencedBlockIds(), error)) return false;
    m_liveBlockDirectory = directory;
    return true;
}

void Document::setBlockCacheLimitBytes(uint64_t bytes) {
    m_store.setCacheLimitBytes(bytes);
}

uint64_t Document::blockCacheBytes() const {
    return m_store.cacheBytes();
}

nlohmann::json Document::toJson(bool includeBlockAudio) const {
    if (m_source.empty())
        throw std::runtime_error("document has no source audio");
    if (m_sourceBlock == kInvalidAudioBlock)
        throw std::runtime_error("document has no source block");

    const std::set<AudioBlockId> blockIds = referencedBlockIds();

    nlohmann::json root;
    root["format"] = "vwave-project";
    root["version"] = 2;
    root["sourceBlock"] = m_sourceBlock;
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
    root["regions"] = regionsToJson(m_arrangement.regions());

    if (includeBlockAudio) {
        root["blocks"] = nlohmann::json::array();
        for (const AudioBlockId id : blockIds) {
            const AudioBlockInfo info = m_store.info(id);
            if (info.id == kInvalidAudioBlock)
                throw std::runtime_error(
                    "document references a missing audio block");
            nlohmann::json block = audioBlockInfoToJson(info);
            block["audio"] = audioToJson(m_store.get(id));
            root["blocks"].push_back(std::move(block));
        }
    }

    root["history"] = nlohmann::json::array();
    for (const auto& entry : m_history) {
        root["history"].push_back(
            {{"operation", entry.operation.toJson()},
             {"before", selectionToJson(entry.beforeSelection)},
             {"after", selectionToJson(entry.afterSelection)},
             {"beforeRegions", regionsToJson(entry.before)},
             {"afterRegions", regionsToJson(entry.after)}});
    }
    return root;
}

bool Document::fromJson(const nlohmann::json& root, Document& out,
                        std::string* error,
                        const AudioBlockStore* externalBlocks) {
    if (error) error->clear();
    try {
        if (root.value("format", std::string{}) != "vwave-project") {
            setError(error, "not a VWaveEditor project");
            return false;
        }
        const int version = root.value("version", 0);
        if (version != 1 && version != 2) {
            setError(error, "unsupported project version");
            return false;
        }

        Document loaded;
        if (version == 1) {
            Audio source;
            if (!audioFromJson(root.at("source"), source, error))
                return false;
            if (source.empty()) {
                setError(error, "project source audio is empty");
                return false;
            }
            loaded = Document::fromAudio(std::move(source));
        } else {
            if (externalBlocks) {
                loaded.m_store = *externalBlocks;
                loaded.m_liveBlockDirectory =
                    externalBlocks->backingDirectory();
            } else {
                if (!root.contains("blocks") || !root["blocks"].is_array()) {
                    setError(error, "project has no audio blocks");
                    return false;
                }
                for (const auto& value : root["blocks"]) {
                    AudioBlockInfo info;
                    if (!audioBlockInfoFromJson(value, info, error))
                        return false;
                    if (!value.contains("audio")) {
                        setError(error, "audio block is missing its data");
                        return false;
                    }
                    Audio audio;
                    if (!audioFromJson(value["audio"], audio, error))
                        return false;
                    if (!loaded.m_store.restore(info, audio, error))
                        return false;
                }
            }
            const AudioBlockId sourceBlock =
                root.value("sourceBlock", uint64_t{0});
            if (sourceBlock == kInvalidAudioBlock) {
                setError(error, "project has no source block");
                return false;
            }
            loaded.m_sourceBlock = sourceBlock;
            if (!loaded.m_store.tryGet(sourceBlock, loaded.m_source, error))
                return false;
            std::vector<AudioRegion> regions;
            if (root.contains("regions")) {
                if (!regionsFromJson(root["regions"], regions, error))
                    return false;
            } else {
                RegionArrangement builder;
                if (!builder.makeFull(sourceBlock, loaded.m_source.numFrames(),
                                      regions, error))
                    return false;
            }
            if (!loaded.m_arrangement.validate(regions, loaded.m_store, error))
                return false;
            loaded.m_arrangement.assign(std::move(regions));
            loaded.m_arrangedValid = false;
            loaded.m_clipViewValid = false;
            const AudioBlockInfo sourceInfo =
                loaded.m_store.info(sourceBlock);
            loaded.m_channels = sourceInfo.channels;
            loaded.m_sampleRate = sourceInfo.sampleRate;
        }

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

        if (version == 1) {
            if (root.contains("history") && root["history"].is_array()) {
                bool clipMode = false;
                bool baseCaptured = false;
                AudioBlockId baseBlock = kInvalidAudioBlock;
                int64_t baseFrames = 0;
                for (const auto& item : root["history"]) {
                    Operation operation;
                    if (!Operation::fromJson(item.at("operation"), operation,
                                             error))
                        return false;
                    const Selection savedBefore = selectionFromJson(
                        item.value("before", nlohmann::json{}));
                    Selection savedAfter = selectionFromJson(
                        item.value("after", nlohmann::json{}));

                    if (operation.kind == Operation::Kind::SetClips) {
                        std::vector<Clip> clips;
                        if (!clipsFromOperation(operation, clips, error))
                            return false;
                        if (!baseCaptured) {
                            const Audio base = loaded.audio();
                            if (base.empty()) {
                                setError(error, "legacy clip base is empty");
                                return false;
                            }
                            baseBlock =
                                loaded.m_store.add(base, "Legacy clip base");
                            baseFrames = base.numFrames();
                            baseCaptured = true;
                        }
                        std::vector<AudioRegion> regions;
                        if (!loaded.regionsFromClips(clips, baseBlock,
                                                     baseFrames, regions,
                                                     error))
                            return false;
                        loaded.m_selection = clampSelection(
                            savedBefore, loaded.numFrames());
                        if (!loaded.commitRegions(
                                operation, std::move(regions), savedBefore,
                                savedAfter, error))
                            return false;
                        clipMode = true;
                    } else if (operation.kind ==
                               Operation::Kind::FlattenClips) {
                        if (!clipMode) {
                            setError(error, "legacy flatten has no clips");
                            return false;
                        }
                        const Audio flat = loaded.audio();
                        AudioRegion region;
                        region.block =
                            loaded.m_store.add(flat, "Legacy flattened");
                        region.sourceLength = flat.numFrames();
                        std::vector<AudioRegion> regions{region};
                        loaded.m_selection = clampSelection(
                            savedBefore, loaded.numFrames());
                        if (!loaded.commitRegions(
                                operation, std::move(regions), savedBefore,
                                savedAfter, error))
                            return false;
                        baseBlock = region.block;
                        baseFrames = flat.numFrames();
                        baseCaptured = true;
                        clipMode = false;
                    } else {
                        if (clipMode) {
                            setError(error,
                                     "legacy sample operation inside clips");
                            return false;
                        }
                        loaded.m_selection = clampSelection(
                            savedBefore, loaded.numFrames());
                        if (!loaded.apply(operation, error)) return false;
                        if (!loaded.m_history.empty()) {
                            HistoryEntry& entry =
                                loaded.m_history.back();
                            entry.beforeSelection = clampSelection(
                                savedBefore, loaded.numFrames());
                            entry.afterSelection = clampSelection(
                                savedAfter, loaded.numFrames());
                            loaded.m_selection = entry.afterSelection;
                        }
                        baseCaptured = false;
                    }
                }
            }
        } else {
            if (root.contains("history") && root["history"].is_array()) {
                for (const auto& item : root["history"]) {
                    Operation operation;
                    if (item.contains("operation") &&
                        !Operation::fromJson(item["operation"], operation,
                                             error))
                        return false;
                    std::vector<AudioRegion> before;
                    std::vector<AudioRegion> after;
                    if (!regionsFromJson(
                            item.value("beforeRegions",
                                       nlohmann::json::array()),
                            before, error))
                        return false;
                    if (!regionsFromJson(
                            item.value("afterRegions",
                                       nlohmann::json::array()),
                            after, error))
                        return false;
                    if (!loaded.m_arrangement.validate(before,
                                                       loaded.m_store,
                                                       error) ||
                        !loaded.m_arrangement.validate(after,
                                                       loaded.m_store,
                                                       error))
                        return false;
                    HistoryEntry entry;
                    entry.operation = std::move(operation);
                    entry.before = std::move(before);
                    entry.after = std::move(after);
                    entry.beforeSelection = selectionFromJson(
                        item.value("before", nlohmann::json{}));
                    entry.afterSelection = selectionFromJson(
                        item.value("after", nlohmann::json{}));
                    entry.stamp = nextHistoryStampForNewAction();
                    loaded.m_history.push_back(std::move(entry));
                }
                loaded.m_redo.clear();
            }
        }

        const int64_t frames = loaded.numFrames();
        loaded.m_markers.erase(
            std::remove_if(loaded.m_markers.begin(), loaded.m_markers.end(),
                           [frames](const Marker& marker) {
                               return marker.frame > frames;
                           }),
            loaded.m_markers.end());
        loaded.m_slices.erase(
            std::remove_if(loaded.m_slices.begin(), loaded.m_slices.end(),
                           [frames](const Slice& slice) {
                               return slice.range.end > frames;
                           }),
            loaded.m_slices.end());
        loaded.m_selection =
            clampSelection(loaded.m_selection, loaded.numFrames());
        loaded.invalidateArranged();
        ++loaded.m_revision;
        out = std::move(loaded);
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
