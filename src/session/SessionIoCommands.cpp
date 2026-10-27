#include "vwave/Session.h"

#include "CommandHelpers.h"

#include <string>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeIoCommands(const std::string& command,
                                          const nlohmann::json& params,
                                          std::string* error) {
        if (command == "save_mixdown") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            const AudioExportOptions options = exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!saveMixdown(path, options, error)) return nullptr;
            return {{"ok", true}, {"path", path}};
        }

        if (command == "load_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!loadAudio(path, error)) return nullptr;
            return info();
        }
        if (command == "save_audio") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            const AudioExportOptions options = exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (optionalBool(params, "mixdown", false)) {
                if (!saveMixdown(path, options, error)) return nullptr;
            } else {
                if (!document().saveAudio(path, options, error)) return nullptr;
            }
            return {{"ok", true}, {"path", path}};
        }
        if (command == "load_project") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!loadProject(path, error)) return nullptr;
            return info();
        }
        if (command == "save_project") {
            const std::string path = requiredString(params, "path", error);
            if (path.empty()) return nullptr;
            if (!saveProject(path, error)) return nullptr;
            return {{"ok", true}, {"path", path}};
        }

    setError(error, "unknown IO command: " + command);
    return nullptr;
}

} // namespace vwave
