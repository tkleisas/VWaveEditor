#include "test_framework.h"

#include "vwave/Mcp.h"
#include "vwave/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

using namespace vwave;
using nlohmann::json;

namespace {

Audio tone() {
    Audio audio(1, 4800, 48000);
    for (int64_t i = 0; i < audio.numFrames(); ++i) {
        audio.sample(0, i) = static_cast<float>(
            0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 *
                           static_cast<double>(i) / 48000.0));
    }
    return audio;
}

json request(int id, const std::string& method, json params = json::object()) {
    json message;
    message["jsonrpc"] = "2.0";
    message["id"] = id;
    message["method"] = method;
    message["params"] = std::move(params);
    return message;
}

json callTool(const std::string& name, json arguments = json::object()) {
    json params;
    params["name"] = name;
    params["arguments"] = std::move(arguments);
    return params;
}

} // namespace

VWTEST("mcp initializes with tools capability") {
    Session session(Document::fromAudio(tone()));
    McpServer server(session);
    const std::string response = server.handleMessage(
        request(1, "initialize",
                {{"protocolVersion", "2024-11-05"},
                 {"capabilities", json::object()},
                 {"clientInfo", {{"name", "test"}, {"version", "0.0.1"}}}})
            .dump());
    REQUIRE(!response.empty());
    const json parsed = json::parse(response);
    CHECK_EQ(parsed["jsonrpc"], std::string("2.0"));
    CHECK_EQ(parsed["id"], 1);
    CHECK(parsed["result"]["capabilities"].contains("tools"));
    CHECK_EQ(parsed["result"]["serverInfo"]["name"], std::string("vwave"));
}

VWTEST("mcp handles notifications without responses") {
    Session session(Document::fromAudio(tone()));
    McpServer server(session);
    CHECK(server.handleMessage(
              R"({"jsonrpc":"2.0","method":"notifications/initialized"})")
              .empty());
    CHECK(server.handleMessage(
              R"({"jsonrpc":"2.0","id":7,"method":"ping"})")
              .find("\"result\"") != std::string::npos);
}

VWTEST("mcp lists tools and resources") {
    Session session(Document::fromAudio(tone()));
    McpServer server(session);
    const json tools = json::parse(server.handleMessage(
        request(2, "tools/list").dump()));
    REQUIRE(tools["result"].contains("tools"));
    const auto& list = tools["result"]["tools"];
    CHECK(list.is_array());
    CHECK(list.size() >= 10);
    bool hasInfo = false;
    bool hasCommand = false;
    for (const auto& tool : list) {
        if (tool["name"] == "vwave_document_info") hasInfo = true;
        if (tool["name"] == "vwave_command") hasCommand = true;
    }
    CHECK(hasInfo);
    CHECK(hasCommand);

    const json resources = json::parse(server.handleMessage(
        request(3, "resources/list").dump()));
    CHECK(resources["result"]["resources"].is_array());
}

VWTEST("mcp tool call edits the document") {
    Session session(Document::fromAudio(tone()));
    McpServer server(session);
    std::string response = server.handleMessage(
        request(4, "tools/call",
                callTool("vwave_command",
                         {{"command", "select"},
                          {"parameters", {{"start", 0.01}, {"end", 0.02}}}}))
            .dump());
    json parsed = json::parse(response);
    CHECK(!parsed["result"]["isError"].get<bool>());
    const json selectResult =
        json::parse(parsed["result"]["content"][0]["text"].get<std::string>());
    CHECK(selectResult["ok"].get<bool>());

    response = server.handleMessage(
        request(5, "tools/call",
                callTool("vwave_command",
                         {{"command", "gain"}, {"parameters", {{"db", -6.0}}}}))
            .dump());
    parsed = json::parse(response);
    CHECK(!parsed["result"]["isError"].get<bool>());
    CHECK_EQ(session.document().undoCount(), size_t{1});
    CHECK_NEAR(session.document().audio().sample(0, 500),
               tone().sample(0, 500) * 0.501187, 1e-5);

    response = server.handleMessage(
        request(6, "tools/call", callTool("vwave_document_info")).dump());
    parsed = json::parse(response);
    CHECK(!parsed["result"]["isError"].get<bool>());
    const json info =
        json::parse(parsed["result"]["content"][0]["text"].get<std::string>());
    CHECK_EQ(info["undo_count"], 1);
}

VWTEST("mcp reports bad requests and bad tools") {
    Session session(Document::fromAudio(tone()));
    McpServer server(session);
    json parsed = json::parse(server.handleMessage("{not json"));
    CHECK_EQ(parsed["error"]["code"], -32700);

    parsed = json::parse(server.handleMessage(
        request(8, "no/such/method").dump()));
    CHECK_EQ(parsed["error"]["code"], -32601);

    parsed = json::parse(server.handleMessage(
        request(9, "tools/call", callTool("no_such_tool")).dump()));
    CHECK(parsed["result"]["isError"].get<bool>());

    parsed = json::parse(server.handleMessage(
        request(10, "tools/call",
                callTool("vwave_command",
                         {{"command", "gain"},
                          {"parameters", {{"db", "loud"}}}}))
            .dump()));
    CHECK(parsed["result"]["isError"].get<bool>());
    CHECK_EQ(session.document().undoCount(), size_t{0});
}

VWTEST("mcp notification tool calls do not respond") {
    Session session(Document::fromAudio(tone()));
    McpServer server(session);
    const std::string message =
        R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"vwave_document_info","arguments":{}}})";
    CHECK(server.handleMessage(message).empty());
}

VWTEST("mcp tool names are stable") {
    const std::vector<std::string> names = McpServer::toolNames();
    CHECK(std::find(names.begin(), names.end(), "vwave_command") != names.end());
    CHECK(std::find(names.begin(), names.end(), "vwave_analyze_beats") !=
          names.end());
    CHECK(std::find(names.begin(), names.end(), "vwave_document_info") !=
          names.end());
}
