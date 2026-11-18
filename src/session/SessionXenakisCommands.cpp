#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/Recipe.h"
#include "vwave/Xenakis.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeXenakisCommands(const std::string& command,
                                               const nlohmann::json& params,
                                               std::string* error) {
    if (command == "xenakis_render") {
        std::string pageText;
        std::string pagePath = params.value("page_path", std::string{});
        if (params.contains("page")) {
            if (!params["page"].is_string()) {
                setError(error, "page must be a string");
                return nullptr;
            }
            pageText = params["page"].get<std::string>();
        } else if (!pagePath.empty()) {
            std::ifstream in(pagePath, std::ios::binary);
            if (!in) {
                setError(error, "cannot open Xenakis page: " + pagePath);
                return nullptr;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            pageText = buffer.str();
        } else {
            setError(error, "xenakis_render requires page or page_path");
            return nullptr;
        }

        const std::string out = requiredString(params, "out", error);
        if (out.empty()) return nullptr;

        XenakisRenderOptions options;
        options.sampleRate = optionalInt(
            params, "sample_rate",
            m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000);
        options.duration = optionalDouble(params, "duration", 0.0);
        options.gain = optionalDouble(params, "gain", 1.0);
        options.normalize = optionalBool(params, "normalize", true);
        options.peak = optionalDouble(params, "peak", 0.9);
        options.loop = optionalBool(params, "loop", false);

        const Audio rendered =
            renderXenakisPage(pageText, options, error);
        if (rendered.empty()) return nullptr;

        AudioExportOptions exportOptions = exportOptionsFrom(params, error);
        if (error && !error->empty()) return nullptr;
        if (!writeAudio(out, rendered, exportOptions, error))
            return nullptr;

        if (optionalBool(params, "replace", false)) {
            int trackIndex = -1;
            if (m_timeline.empty()) {
                if (!m_timeline.addAudioTrack("Xenakis", rendered, 0,
                                              error))
                    return nullptr;
                trackIndex = 0;
            } else {
                const int index = m_timeline.activeTrack();
                if (!m_timeline.setTrackAudio(
                        index, rendered, m_timeline.track(index).startFrame,
                        error))
                    return nullptr;
                trackIndex = index;
            }
            if (trackIndex >= 0)
                m_timeline.track(trackIndex).recipe =
                    makeXenakisTrackRecipe(pageText, options);
        }

        std::string name = "Xenakis";
        if (!pagePath.empty()) {
            const std::string stem =
                std::filesystem::path(pagePath).stem().string();
            if (!stem.empty()) name = stem;
        }
        return {{"ok", true},
                {"path", out},
                {"name", name},
                {"sample_rate", rendered.sampleRate()},
                {"channels", rendered.numChannels()},
                {"frames", rendered.numFrames()},
                {"duration", rendered.duration()}};
    }

    setError(error, "unknown command: " + command);
    return nullptr;
}

} // namespace vwave
