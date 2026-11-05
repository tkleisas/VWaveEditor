#pragma once

#include "vwave/AudioBlockStore.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct AudioRegion {
    AudioBlockId block = kInvalidAudioBlock;
    int64_t sourceStart = 0;
    int64_t sourceLength = 0;
    int64_t timelineStart = 0;
    double stretch = 1.0;
    double gainDb = 0.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    std::string name;
};

int64_t regionOutputFrames(const AudioRegion& region);
nlohmann::json audioRegionToJson(const AudioRegion& region);
bool audioRegionFromJson(const nlohmann::json& json, AudioRegion& out,
                         std::string* error = nullptr);

class RegionArrangement {
public:
    bool empty() const { return m_regions.empty(); }
    size_t size() const { return m_regions.size(); }
    const std::vector<AudioRegion>& regions() const { return m_regions; }
    void assign(std::vector<AudioRegion> regions) {
        m_regions = std::move(regions);
    }
    void clear() { m_regions.clear(); }

    int indexAt(int64_t frame) const;
    int64_t durationFrames() const;
    bool validate(const AudioBlockStore& store,
                  std::string* error = nullptr) const;
    bool validate(const std::vector<AudioRegion>& regions,
                  const AudioBlockStore& store,
                  std::string* error = nullptr) const;

    Audio render(const AudioBlockStore& store,
                 std::string* error = nullptr) const;
    static Audio render(const AudioBlockStore& store,
                        const std::vector<AudioRegion>& regions,
                        std::string* error = nullptr);

    bool makeFull(AudioBlockId block, int64_t sourceLength,
                  std::vector<AudioRegion>& out,
                  std::string* error = nullptr) const;
    bool splitAt(int64_t frame, std::vector<AudioRegion>& out,
                 std::string* error = nullptr) const;
    bool splitAllAt(int64_t frame, std::vector<AudioRegion>& out,
                    std::string* error = nullptr) const;
    bool eraseRange(int64_t start, int64_t end,
                    std::vector<AudioRegion>& out,
                    std::string* error = nullptr) const;
    bool insertRegion(int64_t timelineStart, AudioBlockId block,
                      int64_t sourceLength, std::vector<AudioRegion>& out,
                      std::string* error = nullptr) const;
    bool replaceRange(int64_t start, int64_t end, AudioBlockId block,
                      int64_t sourceLength, std::vector<AudioRegion>& out,
                      std::string* error = nullptr) const;
    bool cropRange(int64_t start, int64_t end,
                   std::vector<AudioRegion>& out,
                   std::string* error = nullptr) const;
    bool remove(size_t index, std::vector<AudioRegion>& out,
                std::string* error = nullptr) const;
    bool move(size_t index, int64_t timelineStart,
              std::vector<AudioRegion>& out,
              std::string* error = nullptr) const;
    bool stretch(size_t index, double ratio, std::vector<AudioRegion>& out,
                 std::string* error = nullptr) const;
    bool repeat(size_t index, int count, std::vector<AudioRegion>& out,
                std::string* error = nullptr) const;
    bool setGain(size_t index, double gainDb,
                 std::vector<AudioRegion>& out,
                 std::string* error = nullptr) const;
    bool setFades(size_t index, double fadeInSeconds, double fadeOutSeconds,
                  std::vector<AudioRegion>& out,
                  std::string* error = nullptr) const;

private:
    std::vector<AudioRegion> m_regions;
};

} // namespace vwave
