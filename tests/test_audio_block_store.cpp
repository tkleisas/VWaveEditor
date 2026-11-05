#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/AudioBlockStore.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace vwave;

namespace {

Audio constantAudio(int channels, int64_t frames, float value,
                    int sampleRate = 48000) {
    Audio audio(channels, frames, sampleRate);
    for (int ch = 0; ch < channels; ++ch)
        for (int64_t frame = 0; frame < frames; ++frame)
            audio.sample(ch, frame) = value + static_cast<float>(ch);
    return audio;
}

void corruptFirstByte(const std::filesystem::path& path) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(file.good());
    char value = 0;
    file.read(&value, 1);
    value = static_cast<char>(value ^ 0x5a);
    file.seekp(0);
    file.write(&value, 1);
    file.flush();
}

} // namespace

VWTEST("audio block store keeps immutable copy-on-write blocks") {
    AudioBlockStore store;
    const Audio original = constantAudio(2, 8, 0.25f);
    const AudioBlockId first = store.add(original, "original");
    const AudioBlockId second = store.add(constantAudio(1, 4, -0.5f), "other");
    CHECK(first != second);
    CHECK(first != kInvalidAudioBlock);
    CHECK(store.size() == size_t{2});
    CHECK(store.contains(first));
    CHECK(!store.contains(999));

    Audio copy;
    REQUIRE(store.tryGet(first, copy));
    CHECK(copy.equals(original));
    copy.sample(0, 0) = 99.0f;
    Audio reloaded;
    REQUIRE(store.tryGet(first, reloaded));
    CHECK_NEAR(reloaded.sample(0, 0), 0.25, 1e-9);

    const AudioBlockInfo info = store.info(first);
    CHECK_EQ(info.id, first);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.frames, int64_t{8});
    CHECK_EQ(info.sampleRate, 48000);
    CHECK_EQ(info.name, std::string("original"));
    CHECK(info.bytes == 2 * 8 * sizeof(float));
}

VWTEST("audio block store persists and reloads blocks") {
    TempDirectory directory;
    AudioBlockStore store;
    const Audio tones = constantAudio(2, 32, 0.125f, 44100);
    const AudioBlockId id = store.add(tones, "tones");
    std::string error;
    REQUIRE(store.save(directory.path(), &error));
    CHECK(error.empty());
    CHECK(std::filesystem::exists(directory.path() / "blocks.json"));
    CHECK(std::filesystem::exists(directory.path() / "1.f32"));
    CHECK(store.backingDirectory() == directory.path());

    AudioBlockStore loaded;
    REQUIRE(loaded.load(directory.path(), &error));
    CHECK(error.empty());
    CHECK_EQ(loaded.size(), size_t{1});
    CHECK(loaded.contains(id));
    const AudioBlockInfo info = loaded.info(id);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.frames, int64_t{32});
    CHECK_EQ(info.sampleRate, 44100);
    Audio audio;
    REQUIRE(loaded.tryGet(id, audio, &error));
    CHECK(audio.equals(tones));
}

VWTEST("audio block store evicts and reloads persisted blocks") {
    TempDirectory directory;
    AudioBlockStore store;
    const Audio firstAudio = constantAudio(1, 100, 0.1f);
    const Audio secondAudio = constantAudio(1, 100, 0.2f);
    const AudioBlockId first = store.add(firstAudio, "first");
    const AudioBlockId second = store.add(secondAudio, "second");
    std::string error;
    REQUIRE(store.save(directory.path(), &error));
    CHECK_EQ(store.cachedBlockCount(), size_t{2});
    CHECK(store.cacheBytes() == 2 * 100 * sizeof(float));

    store.setCacheLimitBytes(100 * sizeof(float));
    CHECK_EQ(store.cachedBlockCount(), size_t{1});
    CHECK(store.cacheBytes() <= 100 * sizeof(float));

    Audio loadedFirst;
    REQUIRE(store.tryGet(first, loadedFirst, &error));
    CHECK(loadedFirst.equals(firstAudio));
    CHECK_EQ(store.cachedBlockCount(), size_t{1});
    Audio loadedSecond;
    REQUIRE(store.tryGet(second, loadedSecond, &error));
    CHECK(loadedSecond.equals(secondAudio));
    CHECK(store.cacheBytes() <= 100 * sizeof(float));
}

VWTEST("audio block store refuses unsafe eviction before persistence") {
    AudioBlockStore store;
    const AudioBlockId id = store.add(constantAudio(1, 16, 0.5f));
    std::string error;
    CHECK(!store.evict(id, &error));
    CHECK(!error.empty());
    CHECK_EQ(store.cachedBlockCount(), size_t{1});
}

VWTEST("audio block store detects corrupt block data") {
    TempDirectory directory;
    AudioBlockStore store;
    const AudioBlockId id = store.add(constantAudio(1, 16, 0.5f));
    std::string error;
    REQUIRE(store.save(directory.path(), &error));
    corruptFirstByte(directory.path() / "1.f32");

    AudioBlockStore loaded;
    REQUIRE(loaded.load(directory.path(), &error));
    Audio audio;
    CHECK(!loaded.tryGet(id, audio, &error));
    CHECK(!error.empty());
}

VWTEST("audio block store rejects missing block files") {
    TempDirectory directory;
    AudioBlockStore store;
    REQUIRE(store.add(constantAudio(1, 16, 0.5f)) != kInvalidAudioBlock);
    std::string error;
    REQUIRE(store.save(directory.path(), &error));
    REQUIRE(std::filesystem::remove(directory.path() / "1.f32"));

    AudioBlockStore loaded;
    CHECK(!loaded.load(directory.path(), &error));
    CHECK(!error.empty());
}

VWTEST("audio block store rejects unsafe manifest file names") {
    TempDirectory directory;
    const auto path = directory.path() / "blocks.json";
    std::ofstream out(path);
    out << R"({"format":"vwave-audio-blocks","version":1,"nextId":2,)"
           R"("blocks":[{"id":1,"channels":1,"frames":4,"sampleRate":48000,)"
           R"("checksum":0,"bytes":16,"name":"bad","file":"../bad.f32"}]})";
    out.close();

    AudioBlockStore store;
    std::string error;
    CHECK(!store.load(directory.path(), &error));
    CHECK(!error.empty());
}

VWTEST("audio block info round-trips through json") {
    AudioBlockInfo info;
    info.id = 42;
    info.channels = 2;
    info.frames = 100;
    info.sampleRate = 96000;
    info.checksum = 123456789;
    info.bytes = 800;
    info.name = "block";
    info.file = "42.f32";
    const auto json = audioBlockInfoToJson(info);

    AudioBlockInfo parsed;
    std::string error;
    REQUIRE(audioBlockInfoFromJson(json, parsed, &error));
    CHECK_EQ(parsed.id, info.id);
    CHECK_EQ(parsed.channels, info.channels);
    CHECK_EQ(parsed.frames, info.frames);
    CHECK_EQ(parsed.sampleRate, info.sampleRate);
    CHECK_EQ(parsed.checksum, info.checksum);
    CHECK_EQ(parsed.bytes, info.bytes);
    CHECK_EQ(parsed.name, info.name);
    CHECK_EQ(parsed.file, info.file);
}

VWTEST("audio block store copy preserves ids and shares samples") {
    AudioBlockStore store;
    const Audio audio = constantAudio(1, 32, 0.75f);
    const AudioBlockId id = store.add(audio, "copy source");
    AudioBlockStore copy = store;
    CHECK(copy.contains(id));
    Audio loaded;
    REQUIRE(copy.tryGet(id, loaded));
    CHECK(loaded.equals(audio));
    const AudioBlockId added = copy.add(constantAudio(1, 8, 0.25f), "new");
    CHECK(added != id);
    CHECK(store.size() == size_t{1});
    CHECK(copy.size() == size_t{2});
}

VWTEST("audio block store restores persisted block ids") {
    AudioBlockStore store;
    const Audio audio = constantAudio(2, 16, 0.5f);
    const AudioBlockId original = store.add(audio, "original");
    const AudioBlockInfo info = store.info(original);

    AudioBlockStore restored;
    std::string error;
    REQUIRE(restored.restore(info, audio, &error));
    CHECK(error.empty());
    CHECK(restored.contains(original));
    Audio loaded;
    REQUIRE(restored.tryGet(original, loaded));
    CHECK(loaded.equals(audio));
    CHECK(!restored.restore(info, audio, &error));
    CHECK(!error.empty());
}

VWTEST("audio block store retain releases unreferenced blocks") {
    AudioBlockStore store;
    const AudioBlockId first = store.add(constantAudio(1, 8, 0.1f));
    const AudioBlockId second = store.add(constantAudio(1, 8, 0.2f));
    store.retain({first});
    CHECK(store.size() == size_t{1});
    CHECK(store.contains(first));
    CHECK(!store.contains(second));
    Audio audio;
    REQUIRE(store.tryGet(first, audio));
    CHECK_NEAR(audio.sample(0, 0), 0.1, 1e-6);
}
