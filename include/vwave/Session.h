#pragma once

#include "vwave/Document.h"
#include "vwave/Timeline.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace vwave {

class Session {
public:
    Session() = default;
    explicit Session(Document document);

    Document& document();
    const Document& document() const;
    Timeline& timeline() { return m_timeline; }
    const Timeline& timeline() const { return m_timeline; }

    bool loadAudio(const std::string& path, std::string* error = nullptr);
    bool saveAudio(const std::string& path, const AudioExportOptions& options,
                   std::string* error = nullptr);
    bool saveMixdown(const std::string& path,
                     const AudioExportOptions& options,
                     std::string* error = nullptr);
    bool loadProject(const std::string& path, std::string* error = nullptr);
    bool saveProject(const std::string& path, std::string* error = nullptr);

    nlohmann::json info();
    nlohmann::json tracksInfo() const;
    void syncSelectionFromActive();
    nlohmann::json execute(const std::string& command,
                           const nlohmann::json& parameters = nlohmann::json::object(),
                           std::string* error = nullptr);
    static std::vector<std::string> commandNames();

private:
    nlohmann::json executeTrackCommands(const std::string& command,
                                        const nlohmann::json& params,
                                        std::string* error);
    nlohmann::json executeAnalysisCommands(const std::string& command,
                                           const nlohmann::json& params,
                                           std::string* error);
    nlohmann::json executeEditCommands(const std::string& command,
                                       const nlohmann::json& params,
                                       std::string* error);
    nlohmann::json executeGeneratorCommands(const std::string& command,
                                            const nlohmann::json& params,
                                            std::string* error);
    nlohmann::json executeClipCommands(const std::string& command,
                                       const nlohmann::json& params,
                                       std::string* error);

    Timeline m_timeline;
    Document m_emptyDocument;
};

} // namespace vwave
