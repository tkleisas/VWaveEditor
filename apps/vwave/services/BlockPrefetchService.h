#pragma once

#include "UiDispatcher.h"
#include "vwave/Audio.h"
#include "vwave/JobSystem.h"
#include "vwave/Timeline.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace vwave::app {

struct PrefetchedBlock {
    int trackIndex = -1;
    vwave::AudioBlockId block = vwave::kInvalidAudioBlock;
    vwave::Audio audio;
};

class BlockPrefetchService {
public:
    using DoneSink = std::function<void(uint64_t generation,
                                        std::vector<PrefetchedBlock> blocks)>;

    BlockPrefetchService(vwave::JobSystem& jobs,
                         UiDispatcher& dispatcher);

    void prefetch(const vwave::Timeline& timeline, uint64_t generation,
                  DoneSink done);
    void cancel();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
};

} // namespace vwave::app
