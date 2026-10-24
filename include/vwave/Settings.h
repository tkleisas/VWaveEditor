#pragma once

#include <string>

namespace vwave {

struct AppSettings {
    std::string inputDevice;
    std::string outputDevice;
    std::string theme = "dark";

    bool save(const std::string& path, std::string* error = nullptr) const;
    static bool load(const std::string& path, AppSettings& out,
                     std::string* error = nullptr);
};

} // namespace vwave
