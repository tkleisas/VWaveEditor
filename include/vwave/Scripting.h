#pragma once

#include "vwave/Session.h"

#include <memory>
#include <string>

struct lua_State;

namespace vwave {

class ScriptEngine {
public:
    explicit ScriptEngine(Session& session);
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    bool runString(const std::string& code, std::string* error = nullptr);
    bool runFile(const std::string& path, std::string* error = nullptr);
    lua_State* state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vwave
