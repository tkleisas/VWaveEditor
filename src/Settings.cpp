#include "vwave/Settings.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace vwave {

namespace {
void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}
} // namespace

bool AppSettings::save(const std::string& path, std::string* error) const {
    if (error) error->clear();
    try {
        nlohmann::json root;
        root["format"] = "vwave-settings";
        root["version"] = 1;
        root["inputDevice"] = inputDevice;
        root["outputDevice"] = outputDevice;
        root["theme"] = theme;
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            setError(error, "cannot create settings file");
            return false;
        }
        out << root.dump(2) << '\n';
        if (!out.good()) {
            setError(error, "failed while writing settings");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot save settings: ") + e.what());
        return false;
    }
}

bool AppSettings::load(const std::string& path, AppSettings& out,
                       std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            setError(error, "cannot open settings file");
            return false;
        }
        nlohmann::json root;
        in >> root;
        if (root.value("format", std::string{}) != "vwave-settings") {
            setError(error, "not a VWaveEditor settings file");
            return false;
        }
        if (root.value("version", 0) != 1) {
            setError(error, "unsupported settings version");
            return false;
        }
        AppSettings loaded;
        loaded.inputDevice = root.value("inputDevice", std::string{});
        loaded.outputDevice = root.value("outputDevice", std::string{});
        loaded.theme = root.value("theme", std::string{"dark"});
        if (loaded.theme != "dark" && loaded.theme != "light")
            loaded.theme = "dark";
        out = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load settings: ") + e.what());
        return false;
    }
}

} // namespace vwave
