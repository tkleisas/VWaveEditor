#include "services/BlockPrefetchService.h"

#include <set>
#include <utility>

namespace vwave::app {

namespace {

struct BlockRequest {
    int trackIndex = -1;
    vwave::AudioBlockId block = vwave::kInvalidAudioBlock;
};

} // namespace

BlockPrefetchService::BlockPrefetchService(vwave::JobSystem& jobs,
                                           UiDispatcher& dispatcher)
    : m_jobs(jobs), m_dispatcher(dispatcher) {}

void BlockPrefetchService::prefetch(const vwave::Timeline& timeline,
                                    uint64_t generation, DoneSink done) {
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    std::vector<BlockRequest> requests;
    for (int trackIndex = 0; trackIndex < timeline.trackCount();
         ++trackIndex) {
        const auto& document = timeline.track(trackIndex).document;
        std::set<vwave::AudioBlockId> ids;
        for (const auto& region : document.regions())
            ids.insert(region.block);
        for (const vwave::AudioBlockId id : ids)
            requests.push_back({trackIndex, id});
    }
    if (requests.empty()) {
        done(generation, {});
        return;
    }

    vwave::Timeline snapshot = timeline;
    m_job = m_jobs.submit(
        [this, request, generation, requests = std::move(requests),
         snapshot = std::move(snapshot), done = std::move(done)](
            vwave::JobControl& control) mutable {
            std::vector<PrefetchedBlock> loaded;
            loaded.reserve(requests.size());
            for (const auto& requestItem : requests) {
                if (control.cancelled()) return;
                if (requestItem.trackIndex < 0 ||
                    requestItem.trackIndex >= snapshot.trackCount())
                    continue;
                vwave::Audio audio;
                if (!snapshot.track(requestItem.trackIndex)
                         .document.loadBlockData(requestItem.block, audio))
                    continue;
                loaded.push_back({requestItem.trackIndex, requestItem.block,
                                  std::move(audio)});
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, generation, loaded = std::move(loaded),
                 done = std::move(done)]() mutable {
                    if (request != m_request) return;
                    done(generation, std::move(loaded));
                });
        });
}

void BlockPrefetchService::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
