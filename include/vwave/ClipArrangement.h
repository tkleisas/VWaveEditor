#pragma once

#include "vwave/Audio.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct Clip {
    int64_t sourceStart = 0;
    int64_t sourceLength = 0;
    int64_t timelineStart = 0;
    double stretch = 1.0;
    double gainDb = 0.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    std::string name;
};

int64_t clipOutputFrames(const Clip& clip);
nlohmann::json clipToJson(const Clip& clip);
bool clipFromJson(const nlohmann::json& json, Clip& out,
                  std::string* error = nullptr);

class ClipArrangement {
public:
    bool empty() const { return m_clips.empty(); }
    size_t size() const { return m_clips.size(); }
    const std::vector<Clip>& clips() const { return m_clips; }
    void assign(std::vector<Clip> clips) { m_clips = std::move(clips); }
    void clear() { m_clips.clear(); }

    int indexAt(int64_t frame) const;
    bool validate(const std::vector<Clip>& clips, int64_t baseFrames,
                  std::string* error) const;
    Audio render(const Audio& base) const;
    static Audio render(const Audio& base, const std::vector<Clip>& clips);

    bool makeFull(int64_t baseFrames, std::vector<Clip>& out,
                  std::string* error) const;
    bool splitAt(int64_t frame, int64_t baseFrames, std::vector<Clip>& out,
                 std::string* error) const;
    bool remove(size_t index, std::vector<Clip>& out,
                std::string* error) const;
    bool move(size_t index, int64_t timelineStart, std::vector<Clip>& out,
              std::string* error) const;
    bool stretch(size_t index, double ratio, std::vector<Clip>& out,
                 std::string* error) const;
    bool repeat(size_t index, int count, std::vector<Clip>& out,
                std::string* error) const;
    bool setGain(size_t index, double gainDb, std::vector<Clip>& out,
                 std::string* error) const;
    bool setFades(size_t index, double fadeInSeconds, double fadeOutSeconds,
                  std::vector<Clip>& out, std::string* error) const;

private:
    std::vector<Clip> m_clips;
};

} // namespace vwave
