#pragma once

#include "vwave/Document.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace vwave {

class Session {
public:
    Session() = default;
    explicit Session(Document document);

    Document& document() { return m_document; }
    const Document& document() const { return m_document; }

    bool loadAudio(const std::string& path, std::string* error = nullptr);
    bool saveAudio(const std::string& path, const AudioExportOptions& options,
                   std::string* error = nullptr);
    bool loadProject(const std::string& path, std::string* error = nullptr);
    bool saveProject(const std::string& path, std::string* error = nullptr);

    nlohmann::json info() const;
    nlohmann::json execute(const std::string& command,
                           const nlohmann::json& parameters = nlohmann::json::object(),
                           std::string* error = nullptr);
    static std::vector<std::string> commandNames();

private:
    Document m_document;
};

} // namespace vwave
