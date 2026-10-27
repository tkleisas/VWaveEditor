#pragma once

#include "AudioPlayer.h"
#include "vwave/Settings.h"

#include <functional>
#include <string>
#include <vector>

namespace vwave::app {

class SettingsController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using ThemeSink = std::function<void(const std::string&)>;

    SettingsController(AudioPlayer& player, StatusSink status,
                       ThemeSink theme);

    const vwave::AppSettings& settings() const { return m_settings; }
    const std::string& path() const { return m_path; }
    const std::vector<std::string>& outputDevices() const {
        return m_outputDevices;
    }
    const std::vector<std::string>& inputDevices() const {
        return m_inputDevices;
    }
    int outputIndex() const { return m_outputIndex; }
    int inputIndex() const { return m_inputIndex; }
    bool isOpen() const { return m_open; }

    void load();
    void save() const;
    void open();
    void close() { m_open = false; }
    void refreshAudioDevices();
    void cycleOutputDevice(int direction);
    void cycleInputDevice(int direction);
    void setTheme(std::string theme);
    void clearOutputDevice();

private:
    AudioPlayer& m_player;
    StatusSink m_status;
    ThemeSink m_theme;
    vwave::AppSettings m_settings;
    std::string m_path;
    std::vector<std::string> m_outputDevices;
    std::vector<std::string> m_inputDevices;
    int m_outputIndex = 0;
    int m_inputIndex = 0;
    bool m_open = false;
};

} // namespace vwave::app
