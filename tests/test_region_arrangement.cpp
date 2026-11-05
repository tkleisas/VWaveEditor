#include "test_framework.h"

#include "vwave/RegionArrangement.h"

#include <cmath>

using namespace vwave;

namespace {

Audio makeRamp(int64_t frames, int channels = 1, int sampleRate = 48000) {
    Audio audio(channels, frames, sampleRate);
    for (int ch = 0; ch < channels; ++ch)
        for (int64_t frame = 0; frame < frames; ++frame)
            audio.sample(ch, frame) =
                static_cast<float>(frame) / static_cast<float>(frames) +
                static_cast<float>(ch) * 0.25f;
    return audio;
}

} // namespace

VWTEST("region arrangement starts as one full reference") {
    AudioBlockStore store;
    const Audio source = makeRamp(100);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    std::string error;
    REQUIRE(arrangement.makeFull(block, source.numFrames(), regions, &error));
    arrangement.assign(regions);
    REQUIRE(arrangement.validate(store, &error));
    CHECK_EQ(arrangement.size(), size_t{1});
    CHECK_EQ(arrangement.durationFrames(), int64_t{100});
    CHECK(arrangement.regions()[0].block == block);
    CHECK_EQ(arrangement.regions()[0].sourceStart, int64_t{0});
    CHECK_EQ(arrangement.regions()[0].sourceLength, int64_t{100});
    CHECK_EQ(arrangement.indexAt(0), 0);
    CHECK_EQ(arrangement.indexAt(99), 0);
    CHECK_EQ(arrangement.indexAt(100), -1);
    CHECK(arrangement.render(store, &error).equals(source));
}

VWTEST("region split creates references without copying audio") {
    AudioBlockStore store;
    const Audio source = makeRamp(100);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 100, regions));
    arrangement.assign(regions);

    std::vector<AudioRegion> split;
    std::string error;
    REQUIRE(arrangement.splitAt(40, split, &error));
    arrangement.assign(split);
    REQUIRE(arrangement.validate(store, &error));
    CHECK_EQ(arrangement.size(), size_t{2});
    CHECK(arrangement.regions()[0].block == block);
    CHECK(arrangement.regions()[1].block == block);
    CHECK_EQ(arrangement.regions()[0].sourceStart, int64_t{0});
    CHECK_EQ(arrangement.regions()[0].sourceLength, int64_t{40});
    CHECK_EQ(arrangement.regions()[1].sourceStart, int64_t{40});
    CHECK_EQ(arrangement.regions()[1].sourceLength, int64_t{60});
    CHECK_EQ(arrangement.regions()[1].timelineStart, int64_t{40});
    CHECK_EQ(arrangement.durationFrames(), int64_t{100});
    CHECK(arrangement.render(store, &error).equals(source));
    CHECK_EQ(store.size(), size_t{1});
}

VWTEST("region split preserves stretched source mapping") {
    AudioBlockStore store;
    const Audio source = makeRamp(100);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 100, regions));
    arrangement.assign(regions);
    REQUIRE(arrangement.stretch(0, 2.0, regions));
    arrangement.assign(regions);
    CHECK_EQ(arrangement.durationFrames(), int64_t{200});

    std::vector<AudioRegion> split;
    REQUIRE(arrangement.splitAt(80, split));
    arrangement.assign(split);
    CHECK_EQ(arrangement.size(), size_t{2});
    CHECK_EQ(arrangement.regions()[0].sourceLength, int64_t{40});
    CHECK_EQ(arrangement.regions()[1].sourceStart, int64_t{40});
    CHECK_EQ(arrangement.regions()[1].sourceLength, int64_t{60});
    CHECK_EQ(arrangement.regions()[0].timelineStart, int64_t{0});
    CHECK_EQ(arrangement.regions()[1].timelineStart, int64_t{80});
    CHECK_EQ(arrangement.durationFrames(), int64_t{200});
}

VWTEST("region move creates gaps and overlap mixes") {
    AudioBlockStore store;
    const Audio source = makeRamp(100);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 100, regions));
    arrangement.assign(regions);
    REQUIRE(arrangement.splitAt(40, regions));
    arrangement.assign(regions);

    std::vector<AudioRegion> moved;
    REQUIRE(arrangement.move(1, 60, moved));
    arrangement.assign(moved);
    CHECK_EQ(arrangement.durationFrames(), int64_t{120});
    const Audio audio = arrangement.render(store);
    for (int64_t frame = 40; frame < 60; ++frame)
        CHECK_NEAR(audio.sample(0, frame), 0.0f, 1e-9);
    for (int64_t frame = 0; frame < 40; ++frame)
        CHECK_NEAR(audio.sample(0, frame), source.sample(0, frame), 1e-9);
    for (int64_t frame = 0; frame < 60; ++frame)
        CHECK_NEAR(audio.sample(0, 60 + frame),
                   source.sample(0, 40 + frame), 1e-9);

    std::vector<AudioRegion> overlapping;
    REQUIRE(arrangement.move(0, 50, overlapping));
    arrangement.assign(overlapping);
    const Audio mixed = arrangement.render(store);
    CHECK_EQ(arrangement.durationFrames(), int64_t{120});
    CHECK_NEAR(mixed.sample(0, 60),
               source.sample(0, 10) + source.sample(0, 40), 1e-6);
}

VWTEST("region repeat ripples later regions") {
    AudioBlockStore store;
    const Audio source = makeRamp(100);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 100, regions));
    arrangement.assign(regions);
    REQUIRE(arrangement.splitAt(40, regions));
    arrangement.assign(regions);

    std::vector<AudioRegion> repeated;
    REQUIRE(arrangement.repeat(1, 2, repeated));
    arrangement.assign(repeated);
    CHECK_EQ(arrangement.size(), size_t{4});
    CHECK_EQ(arrangement.regions()[2].sourceStart, int64_t{40});
    CHECK_EQ(arrangement.regions()[2].timelineStart, int64_t{100});
    CHECK_EQ(arrangement.regions()[3].timelineStart, int64_t{160});
    CHECK_EQ(arrangement.durationFrames(), int64_t{220});
}

VWTEST("region gain and fades shape rendered audio") {
    AudioBlockStore store;
    const Audio source = makeRamp(100);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 100, regions));
    arrangement.assign(regions);
    REQUIRE(arrangement.setGain(0, -6.0, regions));
    arrangement.assign(regions);
    REQUIRE(arrangement.setFades(0, 0.001, 0.001, regions));
    arrangement.assign(regions);
    const Audio audio = arrangement.render(store);
    CHECK_NEAR(audio.sample(0, 0), 0.0f, 1e-9);
    CHECK_NEAR(audio.sample(0, 50),
               source.sample(0, 50) * 0.501187, 1e-5);
    CHECK_NEAR(audio.sample(0, 99), 0.0f, 1e-3);
}

VWTEST("region removal can empty the arrangement") {
    AudioBlockStore store;
    const Audio source = makeRamp(10);
    const AudioBlockId block = store.add(source);
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 10, regions));
    arrangement.assign(regions);
    std::vector<AudioRegion> removed;
    REQUIRE(arrangement.remove(0, removed));
    arrangement.assign(removed);
    CHECK(arrangement.empty());
    CHECK_EQ(arrangement.durationFrames(), int64_t{0});
    CHECK_EQ(arrangement.size(), size_t{0});
    CHECK(arrangement.render(store).empty());
}

VWTEST("region validation rejects bad references and formats") {
    AudioBlockStore store;
    const AudioBlockId block = store.add(makeRamp(100));
    const AudioBlockId other =
        store.add(makeRamp(100, 2));
    RegionArrangement arrangement;
    std::vector<AudioRegion> regions;
    REQUIRE(arrangement.makeFull(block, 100, regions));
    arrangement.assign(regions);
    CHECK(arrangement.validate(store));

    auto bad = regions;
    bad[0].block = 999;
    std::string error;
    CHECK(!arrangement.validate(bad, store, &error));
    CHECK(!error.empty());

    bad = regions;
    bad[0].sourceStart = 90;
    bad[0].sourceLength = 20;
    CHECK(!arrangement.validate(bad, store, &error));

    bad = regions;
    bad[0].stretch = 0.0;
    CHECK(!arrangement.validate(bad, store, &error));

    bad = regions;
    bad[0].fadeInSeconds = -1.0;
    CHECK(!arrangement.validate(bad, store, &error));

    bad = regions;
    bad.push_back(bad[0]);
    bad[1].block = other;
    CHECK(!arrangement.validate(bad, store, &error));
}

VWTEST("region arrangement json round-trips") {
    AudioRegion region;
    region.block = 12;
    region.sourceStart = 3;
    region.sourceLength = 9;
    region.timelineStart = 20;
    region.stretch = 1.5;
    region.gainDb = -3.0;
    region.fadeInSeconds = 0.25;
    region.fadeOutSeconds = 0.5;
    region.name = "Region A";

    const auto json = audioRegionToJson(region);
    AudioRegion parsed;
    std::string error;
    REQUIRE(audioRegionFromJson(json, parsed, &error));
    CHECK(parsed.block == region.block);
    CHECK_EQ(parsed.sourceStart, region.sourceStart);
    CHECK_EQ(parsed.sourceLength, region.sourceLength);
    CHECK_EQ(parsed.timelineStart, region.timelineStart);
    CHECK_NEAR(parsed.stretch, region.stretch, 1e-12);
    CHECK_NEAR(parsed.gainDb, region.gainDb, 1e-12);
    CHECK_NEAR(parsed.fadeInSeconds, region.fadeInSeconds, 1e-12);
    CHECK_NEAR(parsed.fadeOutSeconds, region.fadeOutSeconds, 1e-12);
    CHECK_EQ(parsed.name, region.name);
}
