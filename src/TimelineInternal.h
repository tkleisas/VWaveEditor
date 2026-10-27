#pragma once

#include "vwave/Timeline.h"

#include <nlohmann/json.hpp>
#include <string>

namespace vwave::detail {

inline void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

inline std::string trackName(int index) {
    return "Track " + std::to_string(index + 1);
}

inline nlohmann::json selectionJson(const Selection& selection) {
    return {{"start", selection.start},
            {"end", selection.end},
            {"active", selection.active}};
}

inline Selection selectionFromJson(const nlohmann::json& json) {
    Selection selection;
    if (!json.is_object()) return selection;
    selection.start = json.value("start", int64_t{0});
    selection.end = json.value("end", int64_t{0});
    selection.active = json.value("active", false);
    return selection;
}

} // namespace vwave::detail
