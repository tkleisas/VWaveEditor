#include "services/FileService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace vwave::app {

namespace {

std::string lowerExtension(const std::string& path) {
    std::string extension =
        std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return extension;
}

} // namespace

bool isScopePath(const std::string& path) {
    return lowerExtension(path) == ".vscope";
}

bool isImagePath(const std::string& path) {
    const std::string extension = lowerExtension(path);
    return extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga" || extension == ".gif" ||
           extension == ".ppm" || extension == ".pgm" ||
           extension == ".hdr";
}

std::string defaultSettingsPath() {
    std::filesystem::path base;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        base = appdata;
    } else if (const char* profile = std::getenv("USERPROFILE")) {
        base = std::filesystem::path(profile) / "AppData" / "Roaming";
    } else {
        base = std::filesystem::current_path();
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / ".config";
    else
        base = std::filesystem::current_path();
#endif
    return (base / "vwave" / "settings.json").string();
}

std::string baseName(const std::string& path) {
    if (path.empty()) return "Untitled";
    return std::filesystem::path(path).filename().string();
}

std::string withExtension(std::string path, const std::string& extension) {
    if (path.empty()) return path;
    if (std::filesystem::path(path).extension().empty()) path += extension;
    return path;
}

std::string suggestedMixdownPath(const std::string& projectPath,
                                 const std::string& audioPath) {
    std::filesystem::path base =
        !projectPath.empty() ? std::filesystem::path(projectPath)
                             : std::filesystem::path(audioPath);
    if (base.empty())
        return (std::filesystem::current_path() / "mixdown.wav").string();
    return (base.parent_path() / (base.stem().string() + "_mix.wav")).string();
}

} // namespace vwave::app
