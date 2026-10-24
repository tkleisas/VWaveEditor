#pragma once

#include "vwave/Session.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace vwave {

class McpServer {
public:
    explicit McpServer(Session& session);

    std::string handleMessage(const std::string& message);
    void runStdio(std::istream& input, std::ostream& output);
    static std::vector<std::string> toolNames();

private:
    Session& m_session;
};

} // namespace vwave
