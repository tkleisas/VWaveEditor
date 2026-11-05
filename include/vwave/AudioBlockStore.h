#pragma once

#include "vwave/Audio.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace vwave {

using AudioBlockId = uint64_t;

constexpr AudioBlockId kInvalidAudioBlock = 0;

struct AudioBlockInfo {
    AudioBlockId id = kInvalidAudioBlock;
    int channels = 0;
    int64_t frames = 0;
    int sampleRate = 0;
    uint64_t checksum = 0;
    uint64_t bytes = 0;
    std::string name;
    std::string file;
};

nlohmann::json audioBlockInfoToJson(const AudioBlockInfo& info);
bool audioBlockInfoFromJson(const nlohmann::json& json, AudioBlockInfo& out,
                            std::string* error = nullptr);

class AudioBlockStore {
public:
    AudioBlockStore() = default;
    AudioBlockStore(const AudioBlockStore&) = delete;
    AudioBlockStore& operator=(const AudioBlockStore&) = delete;

    AudioBlockId add(const Audio& audio, std::string name = {});
    bool contains(AudioBlockId id) const;
    bool tryGet(AudioBlockId id, Audio& out,
                std::string* error = nullptr) const;
    Audio get(AudioBlockId id) const;
    AudioBlockInfo info(AudioBlockId id) const;
    std::vector<AudioBlockInfo> blocks() const;
    size_t size() const;
    bool empty() const;
    void clear();

    void setCacheLimitBytes(uint64_t bytes);
    uint64_t cacheLimitBytes() const;
    uint64_t cacheBytes() const;
    size_t cachedBlockCount() const;
    bool evict(AudioBlockId id, std::string* error = nullptr);

    bool save(const std::filesystem::path& directory,
              std::string* error = nullptr);
    bool load(const std::filesystem::path& directory,
              std::string* error = nullptr);
    std::filesystem::path backingDirectory() const;

private:
    struct Entry {
        AudioBlockInfo info;
        Audio data;
        bool loaded = false;
        bool persisted = false;
        uint64_t lastUse = 0;
        uint64_t cacheBytes = 0;
    };

    static uint64_t checksum(const Audio& audio);
    static uint64_t audioBytes(const Audio& audio);
    static std::filesystem::path blockPath(
        const std::filesystem::path& directory, const AudioBlockInfo& info);
    static bool writeBlock(const std::filesystem::path& path,
                           const Audio& audio, std::string* error);
    static bool readBlock(const std::filesystem::path& path, int channels,
                          int64_t frames, int sampleRate, Audio& out,
                          std::string* error);

    Entry* findEntry(AudioBlockId id) const;
    Audio loadEntry(Entry& entry, std::string* error) const;
    void touch(Entry& entry) const;
    void enforceCacheLimit() const;
    bool canEvict(const Entry& entry) const;

    mutable std::mutex m_mutex;
    mutable std::map<AudioBlockId, Entry> m_entries;
    AudioBlockId m_nextId = 1;
    mutable uint64_t m_useClock = 0;
    uint64_t m_cacheLimitBytes = 0;
    std::filesystem::path m_backingDirectory;
};

} // namespace vwave
