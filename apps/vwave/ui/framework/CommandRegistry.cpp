#include "ui/framework/CommandRegistry.h"

#include <algorithm>
#include <utility>

namespace vwave::ui {

bool Shortcut::matches(const KeyEvent& event) const {
    if (key == Key::Unknown || event.key != key) return false;
    return event.ctrl == ctrl && event.shift == shift && event.alt == alt;
}

bool CommandRegistry::add(Command command) {
    if (command.id.empty()) return false;
    if (find(command.id) != nullptr) return false;
    m_commands.push_back(std::move(command));
    return true;
}

bool CommandRegistry::remove(const std::string& id) {
    const auto it = std::find_if(
        m_commands.begin(), m_commands.end(),
        [&id](const Command& command) { return command.id == id; });
    if (it == m_commands.end()) return false;
    m_commands.erase(it);
    return true;
}

bool CommandRegistry::invoke(const std::string& id) {
    const Command* command = find(id);
    if (!command || !command->enabled() || !command->action) return false;
    command->action();
    return true;
}

bool CommandRegistry::executeShortcut(const KeyEvent& event) {
    if (!event.down || event.repeat) return false;
    for (const auto& command : m_commands) {
        if (!command.shortcut.matches(event)) continue;
        if (!command.enabled() || !command.action) return false;
        command.action();
        return true;
    }
    return false;
}

const Command* CommandRegistry::find(const std::string& id) const {
    for (const auto& command : m_commands)
        if (command.id == id) return &command;
    return nullptr;
}

} // namespace vwave::ui
