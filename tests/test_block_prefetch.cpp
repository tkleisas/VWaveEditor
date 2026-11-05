#include "test_framework.h"
#include "test_helpers.h"

#include "services/BlockPrefetchService.h"

#include "vwave/Timeline.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace vwave;
using namespace vwave::app;

namespace {

Audio constantAudio(int64_t frames, float value) {
    Audio audio(1, frames, 48000);
    for (int64_t i = 0; i < frames; ++i) audio.sample(0, i) = value;
    return audio;
}

} // namespace

VWTEST("block prefetch service loads arrangement blocks on a worker") {
    TempDirectory directory;
    const std::string path =
        (directory.path() / "prefetch.vwdoc").string();
    Timeline timeline;
    std::string error;
    REQUIRE(timeline.addAudioTrack("one", constantAudio(400, 0.25f), 0,
                                   &error));
    REQUIRE(timeline.track(0).document.splitClipAt(200, &error));
    REQUIRE(timeline.saveProject(path, &error));

    Timeline loaded;
    REQUIRE(loaded.loadProject(path, &error));
    loaded.track(0).document.setBlockCacheLimitBytes(1);
    CHECK_EQ(loaded.track(0).document.blockCacheBytes(), uint64_t{0});
    loaded.track(0).document.setBlockCacheLimitBytes(0);

    JobSystem jobs(1);
    UiDispatcher dispatcher;
    BlockPrefetchService service(jobs, dispatcher);
    bool done = false;
    uint64_t seenGeneration = 0;
    size_t blockCount = 0;
    service.prefetch(
        loaded, 42,
        [&](uint64_t generation, std::vector<PrefetchedBlock> blocks) {
            seenGeneration = generation;
            blockCount = blocks.size();
            for (auto& block : blocks)
                loaded.track(block.trackIndex)
                    .document.cachePrefetchedBlock(block.block, block.audio);
            done = true;
        });
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        jobs.waitIdle();
        dispatcher.drain();
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(done);
    CHECK_EQ(seenGeneration, uint64_t{42});
    CHECK(blockCount > size_t{0});
    CHECK(loaded.track(0).document.blockCacheBytes() > uint64_t{0});
    CHECK(loaded.track(0).document.audio().equals(
        timeline.track(0).document.audio()));
}
