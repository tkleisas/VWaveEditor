#include "vwave/AudioBlockStore.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace vwave {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool validDimensions(int channels, int64_t frames, std::string* error) {
    if (channels < 0 || frames < 0) {
        setError(error, "block dimensions must be non-negative");
        return false;
    }
    if (channels > 0 && frames >
            std::numeric_limits<int64_t>::max() /
                static_cast<int64_t>(channels)) {
        setError(error, "block dimensions overflow");
        return false;
    }
    return true;
}

bool checkedBytes(int channels, int64_t frames, uint64_t& bytes,
                  std::string* error) {
    bytes = 0;
    if (!validDimensions(channels, frames, error)) return false;
    const uint64_t samples =
        static_cast<uint64_t>(channels) * static_cast<uint64_t>(frames);
    if (samples > std::numeric_limits<uint64_t>::max() / sizeof(float)) {
        setError(error, "block byte size overflows");
        return false;
    }
    bytes = samples * sizeof(float);
    return true;
}

std::string manifestPath(const std::filesystem::path& directory) {
    return (directory / "blocks.json").string();
}

bool isBlockFileName(const std::filesystem::path& path) {
    if (path.extension() != ".f32") return false;
    const std::string stem = path.stem().string();
    return !stem.empty() &&
           std::all_of(stem.begin(), stem.end(), [](unsigned char c) {
               return c >= '0' && c <= '9';
           });
}

} // namespace

nlohmann::json audioBlockInfoToJson(const AudioBlockInfo& info) {
    return {{"id", info.id},
            {"channels", info.channels},
            {"frames", info.frames},
            {"sampleRate", info.sampleRate},
            {"checksum", info.checksum},
            {"bytes", info.bytes},
            {"name", info.name},
            {"file", info.file}};
}

bool audioBlockInfoFromJson(const nlohmann::json& json, AudioBlockInfo& out,
                            std::string* error) {
    if (error) error->clear();
    if (!json.is_object()) {
        setError(error, "audio block must be an object");
        return false;
    }
    try {
        AudioBlockInfo info;
        info.id = json.value("id", uint64_t{0});
        info.channels = json.value("channels", 0);
        info.frames = json.value("frames", int64_t{0});
        info.sampleRate = json.value("sampleRate", 0);
        info.checksum = json.value("checksum", uint64_t{0});
        info.bytes = json.value("bytes", uint64_t{0});
        info.name = json.value("name", std::string{});
        info.file = json.value("file", std::string{});
        if (info.id == kInvalidAudioBlock) {
            setError(error, "audio block id must be non-zero");
            return false;
        }
        if (!validDimensions(info.channels, info.frames, error)) return false;
        if (info.sampleRate <= 0) {
            setError(error, "audio block sample rate must be positive");
            return false;
        }
        uint64_t expected = 0;
        if (!checkedBytes(info.channels, info.frames, expected, error))
            return false;
        if (info.bytes != expected) {
            setError(error, "audio block byte count does not match dimensions");
            return false;
        }
        out = std::move(info);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot parse audio block: ") + e.what());
        return false;
    }
}

uint64_t AudioBlockStore::checksum(const Audio& audio) {
    constexpr uint64_t offset = 1469598103934665603ull;
    constexpr uint64_t prime = 1099511628211ull;
    uint64_t hash = offset;
    const int channels = audio.numChannels();
    const int64_t frames = audio.numFrames();
    for (int ch = 0; ch < channels; ++ch) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(
            audio.channel(ch));
        const size_t count =
            static_cast<size_t>(frames) * sizeof(float);
        for (size_t i = 0; i < count; ++i) {
            hash ^= bytes[i];
            hash *= prime;
        }
    }
    return hash;
}

uint64_t AudioBlockStore::audioBytes(const Audio& audio) {
    return static_cast<uint64_t>(audio.numChannels()) *
           static_cast<uint64_t>(audio.numFrames()) * sizeof(float);
}

std::filesystem::path AudioBlockStore::blockPath(
    const std::filesystem::path& directory, const AudioBlockInfo& info) {
    if (!info.file.empty()) return directory / info.file;
    return directory / (std::to_string(info.id) + ".f32");
}

bool AudioBlockStore::writeBlock(const std::filesystem::path& path,
                                 const Audio& audio, std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        setError(error, "cannot create audio block file");
        return false;
    }
    for (int ch = 0; ch < audio.numChannels(); ++ch) {
        const size_t bytes =
            static_cast<size_t>(audio.numFrames()) * sizeof(float);
        if (bytes > 0) {
            out.write(reinterpret_cast<const char*>(audio.channel(ch)),
                      static_cast<std::streamsize>(bytes));
        }
    }
    if (!out.good()) {
        setError(error, "cannot write audio block file");
        return false;
    }
    return true;
}

bool AudioBlockStore::readBlock(const std::filesystem::path& path,
                                int channels, int64_t frames, int sampleRate,
                                Audio& out, std::string* error) {
    uint64_t expected = 0;
    if (!checkedBytes(channels, frames, expected, error)) return false;
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec || size != expected) {
        setError(error, "audio block file size does not match its metadata");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setError(error, "cannot open audio block file");
        return false;
    }
    Audio result(channels, frames, sampleRate);
    for (int ch = 0; ch < channels; ++ch) {
        const size_t bytes = static_cast<size_t>(frames) * sizeof(float);
        if (bytes > 0) {
            in.read(reinterpret_cast<char*>(result.channel(ch)),
                    static_cast<std::streamsize>(bytes));
        }
    }
    if (!in.good() && !in.eof()) {
        setError(error, "cannot read audio block file");
        return false;
    }
    out = std::move(result);
    return true;
}

AudioBlockStore::Entry* AudioBlockStore::findEntry(AudioBlockId id) const {
    const auto it = m_entries.find(id);
    return it == m_entries.end() ? nullptr : &it->second;
}

void AudioBlockStore::touch(Entry& entry) const {
    entry.lastUse = ++m_useClock;
}

void AudioBlockStore::enforceCacheLimit() const {
    if (m_cacheLimitBytes == 0) return;
    auto usedBytes = [this] {
        uint64_t total = 0;
        for (const auto& item : m_entries) total += item.second.cacheBytes;
        return total;
    };
    while (usedBytes() > m_cacheLimitBytes) {
        Entry* candidate = nullptr;
        for (auto& item : m_entries) {
            Entry& entry = item.second;
            if (!entry.loaded || !canEvict(entry)) continue;
            if (!candidate || entry.lastUse < candidate->lastUse)
                candidate = &entry;
        }
        if (!candidate) break;
        candidate->data = Audio{};
        candidate->loaded = false;
        candidate->cacheBytes = 0;
    }
}

bool AudioBlockStore::canEvict(const Entry& entry) const {
    if (!entry.persisted || m_backingDirectory.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(
        blockPath(m_backingDirectory, entry.info), ec) && !ec;
}

bool AudioBlockStore::contains(AudioBlockId id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return findEntry(id) != nullptr;
}

AudioBlockId AudioBlockStore::add(const Audio& audio, std::string name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Entry entry;
    entry.info.id = m_nextId++;
    entry.info.channels = audio.numChannels();
    entry.info.frames = audio.numFrames();
    entry.info.sampleRate = audio.sampleRate();
    entry.info.checksum = checksum(audio);
    entry.info.bytes = audioBytes(audio);
    entry.info.name = std::move(name);
    entry.info.file = std::to_string(entry.info.id) + ".f32";
    entry.data = audio;
    entry.loaded = true;
    entry.cacheBytes = entry.info.bytes;
    touch(entry);
    const AudioBlockId id = entry.info.id;
    m_entries.emplace(id, std::move(entry));
    enforceCacheLimit();
    return id;
}

bool AudioBlockStore::restore(const AudioBlockInfo& info, const Audio& audio,
                              std::string* error) {
    if (error) error->clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (info.id == kInvalidAudioBlock) {
        setError(error, "audio block id must be non-zero");
        return false;
    }
    if (findEntry(info.id)) {
        setError(error, "audio block id already exists");
        return false;
    }
    if (info.channels != audio.numChannels() ||
        info.frames != audio.numFrames() ||
        info.sampleRate != audio.sampleRate()) {
        setError(error, "audio block metadata does not match its data");
        return false;
    }
    uint64_t expected = 0;
    if (!checkedBytes(info.channels, info.frames, expected, error))
        return false;
    if (info.bytes != expected || info.checksum != checksum(audio)) {
        setError(error, "audio block integrity check failed");
        return false;
    }
    Entry entry;
    entry.info = info;
    if (entry.info.file.empty())
        entry.info.file = std::to_string(info.id) + ".f32";
    entry.data = audio;
    entry.loaded = true;
    entry.persisted = false;
    entry.cacheBytes = info.bytes;
    touch(entry);
    m_entries.emplace(info.id, std::move(entry));
    m_nextId = std::max<uint64_t>(m_nextId, info.id + 1);
    enforceCacheLimit();
    return true;
}

Audio AudioBlockStore::loadEntry(Entry& entry, std::string* error) const {
    if (!entry.loaded) {
        if (!entry.persisted || m_backingDirectory.empty()) {
            setError(error, "audio block is not loaded and has no backing file");
            return {};
        }
        Audio data;
        if (!readBlock(blockPath(m_backingDirectory, entry.info),
                       entry.info.channels, entry.info.frames,
                       entry.info.sampleRate, data, error)) {
            return {};
        }
        if (checksum(data) != entry.info.checksum) {
            setError(error, "audio block checksum mismatch");
            return {};
        }
        entry.data = std::move(data);
        entry.loaded = true;
        entry.cacheBytes = entry.info.bytes;
    }
    touch(entry);
    return entry.data;
}

bool AudioBlockStore::tryGet(AudioBlockId id, Audio& out,
                             std::string* error) const {
    if (error) error->clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    Entry* entry = findEntry(id);
    if (!entry) {
        setError(error, "audio block does not exist");
        return false;
    }
    out = loadEntry(*entry, error);
    if (error && !error->empty()) return false;
    enforceCacheLimit();
    return true;
}

Audio AudioBlockStore::get(AudioBlockId id) const {
    Audio result;
    tryGet(id, result, nullptr);
    return result;
}

AudioBlockInfo AudioBlockStore::info(AudioBlockId id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const Entry* entry = findEntry(id);
    return entry ? entry->info : AudioBlockInfo{};
}

std::vector<AudioBlockInfo> AudioBlockStore::blocks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AudioBlockInfo> result;
    result.reserve(m_entries.size());
    for (const auto& item : m_entries) result.push_back(item.second.info);
    return result;
}

size_t AudioBlockStore::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

bool AudioBlockStore::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.empty();
}

void AudioBlockStore::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_nextId = 1;
    m_useClock = 0;
}

void AudioBlockStore::setCacheLimitBytes(uint64_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cacheLimitBytes = bytes;
    enforceCacheLimit();
}

uint64_t AudioBlockStore::cacheLimitBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cacheLimitBytes;
}

uint64_t AudioBlockStore::cacheBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto& item : m_entries) total += item.second.cacheBytes;
    return total;
}

size_t AudioBlockStore::cachedBlockCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& item : m_entries) {
        if (item.second.loaded) ++count;
    }
    return count;
}

bool AudioBlockStore::evict(AudioBlockId id, std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Entry* entry = findEntry(id);
    if (!entry) {
        setError(error, "audio block does not exist");
        return false;
    }
    if (!entry->loaded) return true;
    if (!canEvict(*entry)) {
        setError(error, "audio block cannot be evicted before it is persisted");
        return false;
    }
    entry->data = Audio{};
    entry->loaded = false;
    entry->cacheBytes = 0;
    return true;
}

bool AudioBlockStore::save(const std::filesystem::path& directory,
                           std::string* error) {
    if (error) error->clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        setError(error, "cannot create audio block directory");
        return false;
    }

    std::vector<AudioBlockInfo> infos;
    infos.reserve(m_entries.size());
    for (auto& item : m_entries) {
        Entry& entry = item.second;
        if (entry.info.file.empty())
            entry.info.file = std::to_string(entry.info.id) + ".f32";
        if (!entry.loaded) {
            std::string loadError;
            loadEntry(entry, &loadError);
            if (!loadError.empty()) {
                setError(error, "cannot save audio blocks: " + loadError);
                return false;
            }
        }
        const auto path = blockPath(directory, entry.info);
        const auto temporary = path.string() + ".tmp";
        if (!writeBlock(temporary, entry.data, error)) return false;
        std::error_code renameError;
        std::filesystem::rename(temporary, path, renameError);
        if (renameError) {
            std::filesystem::remove(temporary, renameError);
            setError(error, "cannot commit audio block file");
            return false;
        }
        entry.persisted = true;
        infos.push_back(entry.info);
        if (entry.info.bytes == 0) {
            entry.cacheBytes = 0;
            entry.loaded = true;
        }
    }

    nlohmann::json root;
    root["format"] = "vwave-audio-blocks";
    root["version"] = 1;
    root["nextId"] = m_nextId;
    root["blocks"] = nlohmann::json::array();
    for (const auto& info : infos)
        root["blocks"].push_back(audioBlockInfoToJson(info));

    const auto temp = directory / "blocks.json.tmp";
    const auto manifest = directory / "blocks.json";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            setError(error, "cannot create audio block manifest");
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            setError(error, "cannot write audio block manifest");
            return false;
        }
    }
    std::filesystem::rename(temp, manifest, ec);
    if (ec) {
        setError(error, "cannot replace audio block manifest");
        return false;
    }

    m_backingDirectory = directory;
    for (auto& item : m_entries) item.second.persisted = true;

    std::vector<std::filesystem::path> expected;
    expected.reserve(infos.size());
    for (const auto& info : infos)
        expected.push_back(directory / info.file);
    std::error_code iteratorError;
    for (const auto& item :
         std::filesystem::directory_iterator(directory, iteratorError)) {
        if (!item.is_regular_file() || !isBlockFileName(item.path())) continue;
        if (std::find(expected.begin(), expected.end(), item.path()) ==
            expected.end()) {
            std::filesystem::remove(item.path(), ec);
            ec.clear();
        }
    }
    return true;
}

bool AudioBlockStore::load(const std::filesystem::path& directory,
                           std::string* error) {
    if (error) error->clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream in(manifestPath(directory), std::ios::binary);
    if (!in) {
        setError(error, "cannot open audio block manifest");
        return false;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot parse audio block manifest: ") +
                            e.what());
        return false;
    }
    if (!root.is_object() ||
        root.value("format", std::string{}) != "vwave-audio-blocks" ||
        root.value("version", 0) != 1) {
        setError(error, "unsupported audio block manifest");
        return false;
    }
    if (!root.contains("blocks") || !root["blocks"].is_array()) {
        setError(error, "audio block manifest has no blocks array");
        return false;
    }

    std::map<AudioBlockId, Entry> entries;
    uint64_t maximumId = 0;
    for (const auto& item : root["blocks"]) {
        AudioBlockInfo info;
        if (!audioBlockInfoFromJson(item, info, error)) return false;
        if (info.file.empty()) {
            setError(error, "audio block has no file name");
            return false;
        }
        const std::filesystem::path file(info.file);
        if (file.has_parent_path() || file.filename() != file) {
            setError(error, "audio block file name is not safe");
            return false;
        }
        Entry entry;
        entry.info = info;
        entry.loaded = false;
        entry.persisted = true;
        const auto path = blockPath(directory, info);
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(path, ec);
        if (ec || size != info.bytes) {
            setError(error, "audio block file is missing or has a wrong size");
            return false;
        }
        if (!entries.emplace(info.id, std::move(entry)).second) {
            setError(error, "duplicate audio block id");
            return false;
        }
        maximumId = std::max(maximumId, info.id);
    }

    uint64_t nextId = root.value("nextId", uint64_t{1});
    nextId = std::max<uint64_t>(nextId, maximumId + 1);
    if (nextId == 0) nextId = 1;
    m_entries = std::move(entries);
    m_nextId = nextId;
    m_useClock = 0;
    m_backingDirectory = directory;
    return true;
}

std::filesystem::path AudioBlockStore::backingDirectory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_backingDirectory;
}
AudioBlockStore::AudioBlockStore(const AudioBlockStore& other) {
    std::lock_guard<std::mutex> lock(other.m_mutex);
    m_entries = other.m_entries;
    m_nextId = other.m_nextId;
    m_useClock = other.m_useClock;
    m_cacheLimitBytes = other.m_cacheLimitBytes;
    m_backingDirectory = other.m_backingDirectory;
}

AudioBlockStore& AudioBlockStore::operator=(const AudioBlockStore& other) {
    if (this == &other) return *this;
    std::scoped_lock lock(m_mutex, other.m_mutex);
    m_entries = other.m_entries;
    m_nextId = other.m_nextId;
    m_useClock = other.m_useClock;
    m_cacheLimitBytes = other.m_cacheLimitBytes;
    m_backingDirectory = other.m_backingDirectory;
    return *this;
}


} // namespace vwave
