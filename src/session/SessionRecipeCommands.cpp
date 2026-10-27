#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Recipe.h"

#include <string>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeRecipeCommands(const std::string& command,
                                             const nlohmann::json& params,
                                             std::string* error) {
        if (command == "regenerate_track") {
            int index = m_timeline.activeTrack();
            if (params.contains("index")) index = params["index"].get<int>();
            if (index < 0 || index >= m_timeline.trackCount()) {
                setError(error, "track index is out of range");
                return nullptr;
            }
            Audio regenerated;
            if (!regenerateTrack(m_timeline, index, regenerated, error))
                return nullptr;
            const int64_t start = m_timeline.track(index).startFrame;
            if (!m_timeline.setTrackAudio(index, std::move(regenerated),
                                          start, error))
                return nullptr;
            m_timeline.setActiveTrack(index);
            return {{"ok", true},
                    {"index", index},
                    {"recipe_type",
                     m_timeline.track(index).recipe.value(
                         "type", std::string{})}};
        }

    setError(error, "unknown command: " + command);
    return nullptr;
}

} // namespace vwave
