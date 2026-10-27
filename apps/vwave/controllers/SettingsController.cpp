#include "controllers/SettingsController.h"

#include "services/FileService.h"
#include "vwave/Settings.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace vwave::app {

SettingsController::SettingsController(AudioPlayer& player,
                                       StatusSink status, ThemeSink theme)
    : m_player(player),
      m_status(std::move(status)),
      m_theme(std::move(theme)),
      m_path(defaultSettingsPath()) {}

void SettingsController::load() {
    std::string error;
    if (!vwave::AppSettings::load(m_path, m_settings, &error)) {
        m_settings = {};
    }
    m_theme(m_settings.theme);
}

void SettingsController::save() const {
    try {
        const std::filesystem::path path(m_path);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
    } catch (...) {
    }
    std::string error;
    if (!m_settings.save(m_path, &error) && !error.empty())
        m_status("Settings save failed: " + error);
}

void SettingsController::open() {
    refreshAudioDevices();
    m_open = true;
}

void SettingsController::refreshAudioDevices() {
    m_outputDevices.clear();
    m_inputDevices.clear();
    m_outputDevices.push_back("");
    m_inputDevices.push_back("");
    const int outputCount = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < outputCount; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name && std::find(m_outputDevices.begin(), m_outputDevices.end(),
                              name) == m_outputDevices.end())
            m_outputDevices.emplace_back(name);
    }
    const int inputCount = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < inputCount; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (name && std::find(m_inputDevices.begin(), m_inputDevices.end(),
                              name) == m_inputDevices.end())
            m_inputDevices.emplace_back(name);
    }
    m_outputIndex = 0;
    m_inputIndex = 0;
    for (size_t i = 0; i < m_outputDevices.size(); ++i) {
        if (m_outputDevices[i] == m_settings.outputDevice) {
            m_outputIndex = static_cast<int>(i);
            break;
        }
    }
    for (size_t i = 0; i < m_inputDevices.size(); ++i) {
        if (m_inputDevices[i] == m_settings.inputDevice) {
            m_inputIndex = static_cast<int>(i);
            break;
        }
    }
}

void SettingsController::cycleOutputDevice(int direction) {
    if (m_outputDevices.empty()) return;
    const int count = static_cast<int>(m_outputDevices.size());
    m_outputIndex = (m_outputIndex + direction + count) % count;
    m_settings.outputDevice =
        m_outputDevices[static_cast<size_t>(m_outputIndex)];
    save();
    m_player.stop();
    m_player.close();
    m_player.setOutputDevice(m_settings.outputDevice);
    if (!m_player.open()) {
        clearOutputDevice();
        if (!m_player.open())
            m_status("Cannot open any audio output device");
        else
            m_status("Output device unavailable; using System Default");
    } else {
        m_status("Output: " + (m_settings.outputDevice.empty()
                                  ? std::string("System Default")
                                  : m_settings.outputDevice));
    }
}

void SettingsController::cycleInputDevice(int direction) {
    if (m_inputDevices.empty()) return;
    const int count = static_cast<int>(m_inputDevices.size());
    m_inputIndex = (m_inputIndex + direction + count) % count;
    m_settings.inputDevice =
        m_inputDevices[static_cast<size_t>(m_inputIndex)];
    save();
    m_status("Input: " + (m_settings.inputDevice.empty()
                              ? std::string("System Default")
                              : m_settings.inputDevice));
}

void SettingsController::setTheme(std::string theme) {
    if (theme != "dark" && theme != "light") return;
    m_settings.theme = std::move(theme);
    m_theme(m_settings.theme);
    save();
    m_status("Theme: " + m_settings.theme);
}

void SettingsController::clearOutputDevice() {
    m_settings.outputDevice.clear();
    m_outputIndex = 0;
    save();
    m_player.setOutputDevice("");
}

} // namespace vwave::app
