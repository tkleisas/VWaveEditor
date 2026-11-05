#pragma once

#include <cstdint>

namespace vwave {

struct HistoryStamp {
    uint64_t sequence = 0;
    uint64_t generation = 0;
};

HistoryStamp nextHistoryStampForNewAction();
void noteUndoPerformed();
uint64_t currentHistorySequence();
uint64_t currentHistoryGeneration();
bool historyRedoPending();

} // namespace vwave
