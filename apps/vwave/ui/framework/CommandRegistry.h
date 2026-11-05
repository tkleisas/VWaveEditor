#pragma once

#include "ui/framework/Event.h"

#include <functional>
#include <string>
#include <vector>

namespace vwave::ui {

struct Shortcut {
    Key key = Key::Unknown;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool matches(const KeyEvent& event) const;
};

struct Command {
    std::string id;
    std::string label;
    Shortcut shortcut;
    std::function<bool()> enabledPredicate;
    std::function<void()> action;

    bool enabled() const {
        return !enabledPredicate || enabledPredicate();
    }
};

class CommandRegistry {
public:
    bool add(Command command);
    bool remove(const std::string& id);
    bool invoke(const std::string& id);
    bool executeShortcut(const KeyEvent& event);

    const Command* find(const std::string& id) const;
    size_t size() const { return m_commands.size(); }

private:
    std::vector<Command> m_commands;
};

} // namespace vwave::ui
