#include "vwave/HistoryStamp.h"

#include <atomic>

namespace vwave {

namespace {

std::atomic<uint64_t> g_sequence{0};
std::atomic<uint64_t> g_generation{1};
std::atomic<bool> g_redoPending{false};

} // namespace

HistoryStamp nextHistoryStampForNewAction() {
    if (g_redoPending.exchange(false))
        g_generation.fetch_add(1);
    return {g_sequence.fetch_add(1) + 1, g_generation.load()};
}

void noteUndoPerformed() { g_redoPending.store(true); }

uint64_t currentHistorySequence() { return g_sequence.load(); }

uint64_t currentHistoryGeneration() { return g_generation.load(); }

bool historyRedoPending() { return g_redoPending.load(); }

} // namespace vwave
