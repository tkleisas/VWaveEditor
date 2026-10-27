#pragma once

#include <string>

namespace vwave::app {

bool isScopePath(const std::string& path);
bool isImagePath(const std::string& path);
std::string defaultSettingsPath();
std::string baseName(const std::string& path);
std::string withExtension(std::string path, const std::string& extension);
std::string suggestedMixdownPath(const std::string& projectPath,
                                 const std::string& audioPath);

} // namespace vwave::app
