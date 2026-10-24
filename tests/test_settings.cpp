#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Settings.h"

#include <filesystem>
#include <fstream>

using namespace vwave;

VWTEST("settings round-trip") {
    AppSettings settings;
    settings.inputDevice = "Microphone";
    settings.outputDevice = "Speakers";
    settings.theme = "light";
    TempDirectory directory;
    const std::string path = (directory.path() / "settings.json").string();
    std::string error;
    REQUIRE(settings.save(path, &error));
    CHECK(error.empty());

    AppSettings loaded;
    REQUIRE(AppSettings::load(path, loaded, &error));
    CHECK_EQ(loaded.inputDevice, std::string("Microphone"));
    CHECK_EQ(loaded.outputDevice, std::string("Speakers"));
    CHECK_EQ(loaded.theme, std::string("light"));
}

VWTEST("settings apply safe defaults") {
    TempDirectory directory;
    const std::string path = (directory.path() / "settings.json").string();
    {
        std::ofstream out(path);
        out << R"({"format":"vwave-settings","version":1,"theme":"neon"})";
    }
    AppSettings loaded;
    std::string error;
    REQUIRE(AppSettings::load(path, loaded, &error));
    CHECK_EQ(loaded.theme, std::string("dark"));
    CHECK(loaded.inputDevice.empty());
    CHECK(loaded.outputDevice.empty());
}

VWTEST("settings reject missing and invalid files") {
    AppSettings settings;
    std::string error;
    CHECK(!AppSettings::load("/no/such/settings.json", settings, &error));
    CHECK(!error.empty());
    TempDirectory directory;
    const std::string path = (directory.path() / "bad.json").string();
    {
        std::ofstream out(path);
        out << "{not json";
    }
    error.clear();
    CHECK(!AppSettings::load(path, settings, &error));
    CHECK(!error.empty());
}
