#include "vwave/Mcp.h"

#include "vwave/Scripting.h"
#include "vwave/Session.h"
#include "vwave/Version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace vwave {

namespace {

using json = nlohmann::json;

struct ToolDef {
    std::string name;
    std::string description;
    std::string command;
    json schema;
    bool generic = false;
    bool lua = false;
};

json objectSchema(json properties, json required = json::array()) {
    json schema;
    schema["type"] = "object";
    schema["properties"] = std::move(properties);
    schema["required"] = std::move(required);
    schema["additionalProperties"] = false;
    return schema;
}

const std::vector<ToolDef>& tools() {
    static const std::vector<ToolDef> definitions = {
        {"vwave_document_info",
         "Return sample rate, duration, selection, history and metadata.",
         "info", objectSchema(json::object()), false, false},
        {"vwave_load_audio",
         "Load an audio file as a new single-track project.",
         "load_audio",
         objectSchema({{"path", {{"type", "string"}}}}, json::array({"path"})),
         false, false},
        {"vwave_save_audio",
         "Write the active track to an audio file.",
         "save_audio",
         objectSchema({{"path", {{"type", "string"}}},
                       {"format", {{"type", "string"},
                                   {"enum", {"auto", "wav", "aiff", "flac", "ogg"}}}},
                       {"bit_depth", {{"type", "string"},
                                      {"enum", {"int16", "int24", "float32"}}}}},
                      json::array({"path"})),
         false, false},
        {"vwave_load_project",
         "Load a non-destructive VWaveEditor project.",
         "load_project",
         objectSchema({{"path", {{"type", "string"}}}}, json::array({"path"})),
         false, false},
        {"vwave_save_project",
         "Save the source, edit history and metadata as a project.",
         "save_project",
         objectSchema({{"path", {{"type", "string"}}}}, json::array({"path"})),
         false, false},
        {"vwave_select",
         "Set the active selection in seconds.",
         "select",
         objectSchema({{"start", {{"type", "number"}}},
                       {"end", {{"type", "number"}}}},
                      json::array({"start", "end"})),
         false, false},
        {"vwave_tracks",
         "List tracks with names, offsets, routing, and active state.",
         "tracks", objectSchema(json::object()), false, false},
        {"vwave_add_track",
         "Add an audio file as a new track.",
         "add_track",
         objectSchema({{"path", {{"type", "string"}}},
                       {"name", {{"type", "string"}}},
                       {"start", {{"type", "number"}}}},
                      json::array({"path"})),
         false, false},
        {"vwave_add_empty_track",
         "Add an empty track for recording or later edits.",
         "add_empty_track",
         objectSchema({{"name", {{"type", "string"}}},
                       {"start", {{"type", "number"}}}}),
         false, false},
        {"vwave_select_track",
         "Select the active track.",
         "select_track",
         objectSchema({{"index", {{"type", "integer"}}}},
                      json::array({"index"})),
         false, false},
        {"vwave_remove_track",
         "Remove a track by index, or the active track.",
         "remove_track",
         objectSchema({{"index", {{"type", "integer"}}}}), false, false},
        {"vwave_track_set",
         "Set track name, offset, gain, pan, mute, solo, or arm state.",
         "track_set",
         objectSchema({{"index", {{"type", "integer"}}},
                       {"name", {{"type", "string"}}},
                       {"start", {{"type", "number"}}},
                       {"gain_db", {{"type", "number"}}},
                       {"pan", {{"type", "number"}}},
                       {"muted", {{"type", "boolean"}}},
                       {"solo", {{"type", "boolean"}}},
                       {"armed", {{"type", "boolean"}}}}),
         false, false},
        {"vwave_save_mixdown",
         "Render all audible tracks to a stereo audio file.",
         "save_mixdown",
         objectSchema({{"path", {{"type", "string"}}},
                       {"format", {{"type", "string"},
                                   {"enum", {"auto", "wav", "aiff", "flac", "ogg"}}}},
                       {"bit_depth", {{"type", "string"},
                                      {"enum", {"int16", "int24", "float32"}}}}},
                      json::array({"path"})),
         false, false},
        {"vwave_undo", "Undo the last edit.", "undo",
         objectSchema(json::object()), false, false},
        {"vwave_redo", "Redo the most recently undone edit.", "redo",
         objectSchema(json::object()), false, false},
        {"vwave_analyze_beats",
         "Estimate tempo, onsets and a beat grid.",
         "analyze_beats",
         objectSchema({{"sensitivity", {{"type", "number"}}}}), false, false},
        {"vwave_detect_pitch",
         "Estimate the fundamental frequency of the selection or a range.",
         "detect_pitch",
         objectSchema({{"start", {{"type", "number"}}},
                       {"end", {{"type", "number"}}},
                       {"min_hz", {{"type", "number"}}},
                       {"max_hz", {{"type", "number"}}}}),
         false, false},
        {"vwave_slices",
         "Create transient, beat or marker-aligned slices.",
         "slices",
         objectSchema({{"mode", {{"type", "string"},
                                 {"enum", {"transients", "beats", "markers"}}}},
                       {"sensitivity", {{"type", "number"}}},
                       {"min_slice_seconds", {{"type", "number"}}},
                       {"bpm", {{"type", "number"}}},
                       {"beats_per_slice", {{"type", "integer"}}}}),
         false, false},
        {"vwave_export_slices",
         "Export the current slices and a JSON manifest.",
         "export_slices",
         objectSchema({{"directory", {{"type", "string"}}},
                       {"prefix", {{"type", "string"}}}},
                      json::array({"directory"})),
         false, false},
        {"vwave_wavetable",
         "Extract a wavetable from the document or build one from harmonics.",
         "wavetable",
         objectSchema({{"path", {{"type", "string"}}},
                       {"frame_size", {{"type", "integer"}}},
                       {"frame_count", {{"type", "integer"}}},
                       {"start", {{"type", "number"}}},
                       {"end", {{"type", "number"}}},
                       {"auto_cycle", {{"type", "boolean"}}},
                       {"cycle_length", {{"type", "integer"}}},
                       {"normalize", {{"type", "boolean"}}},
                       {"amplitudes", {{"type", "array"},
                                       {"items", {{"type", "number"}}}}}},
                      json::array({"path"})),
         false, false},
        {"vwave_synth_note",
         "Render a note from a wavetable and optionally write or replace.",
         "synth_note",
         objectSchema({{"wavetable", {{"type", "string"}}},
                       {"midi_note", {{"type", "number"}}},
                       {"duration", {{"type", "number"}}},
                       {"out", {{"type", "string"}}},
                       {"replace", {{"type", "boolean"}}},
                       {"channels", {{"type", "integer"}}},
                       {"morph", {{"type", "number"}}},
                       {"unison", {{"type", "integer"}}},
                       {"detune_cents", {{"type", "number"}}}},
                      json::array({"wavetable", "midi_note", "duration"})),
         false, false},
        {"vwave_generate_sweep",
         "Generate an exponential sine sweep.",
         "generate_sweep",
         objectSchema({{"out", {{"type", "string"}}},
                       {"seconds", {{"type", "number"}}},
                       {"start_hz", {{"type", "number"}}},
                       {"end_hz", {{"type", "number"}}},
                       {"amplitude", {{"type", "number"}}},
                       {"sample_rate", {{"type", "integer"}}},
                       {"channels", {{"type", "integer"}}},
                       {"replace", {{"type", "boolean"}}}}),
         false, false},
        {"vwave_deconvolve",
         "Extract an impulse response from a recorded sweep.",
         "deconvolve",
         objectSchema({{"recorded", {{"type", "string"}}},
                       {"sweep", {{"type", "string"}}},
                       {"out", {{"type", "string"}}},
                       {"ir_length", {{"type", "integer"}}},
                       {"trim_to_peak", {{"type", "boolean"}}},
                       {"pre_delay", {{"type", "number"}}},
                       {"normalize", {{"type", "boolean"}}},
                       {"manifest", {{"type", "string"}}}},
                      json::array({"recorded", "sweep"})),
         false, false},
        {"vwave_command",
         "Execute any VWaveEditor command with a JSON parameter object.",
         "", 
         objectSchema({{"command", {{"type", "string"},
                                    {"enum", Session::commandNames()}}},
                       {"parameters", {{"type", "object"},
                                       {"additionalProperties", true}}}},
                      json::array({"command"})),
         true, false},
        {"vwave_run_lua",
         "Run Lua code against the current document.",
         "",
         objectSchema({{"code", {{"type", "string"}}}},
                      json::array({"code"})),
         false, true},
    };
    return definitions;
}

json makeError(const json& id, int code, const std::string& message) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = id.is_null() ? json(nullptr) : id;
    response["error"] = {{"code", code}, {"message", message}};
    return response;
}

json makeResult(const json& id, json result) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = std::move(result);
    return response;
}

json textContent(const std::string& text, bool isError,
                 const json& structured = json()) {
    json result;
    result["content"] = json::array(
        {{{"type", "text"}, {"text", text}}});
    result["isError"] = isError;
    if (!structured.is_null()) result["structuredContent"] = structured;
    return result;
}

json toolListJson() {
    json list = json::array();
    for (const auto& tool : tools()) {
        list.push_back({{"name", tool.name},
                        {"description", tool.description},
                        {"inputSchema", tool.schema}});
    }
    return {{"tools", list}};
}

std::string trimCarriageReturn(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

} // namespace

McpServer::McpServer(Session& session) : m_session(session) {}

std::string McpServer::handleMessage(const std::string& message) {
    json request;
    try {
        request = json::parse(message);
    } catch (const std::exception& e) {
        return makeError(nullptr, -32700,
                         std::string("parse error: ") + e.what())
            .dump();
    }
    if (!request.is_object()) {
        return makeError(nullptr, -32600, "invalid request").dump();
    }
    const bool notification = !request.contains("id");
    const json id = notification ? json(nullptr) : request["id"];
    if (request.value("jsonrpc", std::string{}) != "2.0") {
        if (notification) return {};
        return makeError(id, -32600, "jsonrpc must be 2.0").dump();
    }
    if (!request.contains("method") || !request["method"].is_string()) {
        if (notification) return {};
        return makeError(id, -32600, "missing method").dump();
    }
    const std::string method = request["method"].get<std::string>();
    const json params = request.value("params", json::object());

    try {
        if (method == "initialize") {
            if (notification) return {};
            const std::string protocol =
                params.value("protocolVersion", std::string{"2024-11-05"});
            json result;
            result["protocolVersion"] = protocol;
            result["capabilities"] = {
                {"tools", {{"listChanged", false}}},
                {"resources", {{"listChanged", false}, {"subscribe", false}}}};
            result["serverInfo"] = {
                {"name", "vwave"},
                {"title", "VWaveEditor"},
                {"version", VWAVE_VERSION}};
            result["instructions"] =
                "Use vwave_document_info to inspect state and vwave_command "
                "for editing, analysis, spectral, wavetable and capture "
                "operations. Edits are non-destructive and undoable.";
            return makeResult(id, std::move(result)).dump();
        }
        if (method == "notifications/initialized") return {};
        if (method == "ping") {
            if (notification) return {};
            return makeResult(id, json::object()).dump();
        }
        if (method == "tools/list") {
            if (notification) return {};
            return makeResult(id, toolListJson()).dump();
        }
        if (method == "resources/list") {
            if (notification) return {};
            return makeResult(
                id,
                {{"resources",
                  json::array(
                      {{{"uri", "vwave://document/info"},
                        {"name", "Document info"},
                        {"description",
                         "Current document, selection and edit history."},
                        {"mimeType", "application/json"}}})}})
                .dump();
        }
        if (method == "resources/read") {
            if (notification) return {};
            if (!params.contains("uri") || !params["uri"].is_string())
                return makeError(id, -32602, "missing resource uri").dump();
            const std::string uri = params["uri"].get<std::string>();
            if (uri != "vwave://document/info")
                return makeError(id, -32602, "unknown resource").dump();
            return makeResult(
                id,
                {{"contents",
                  json::array({{{ "uri", uri},
                                {"mimeType", "application/json"},
                                {"text", m_session.info().dump()}}})}})
                .dump();
        }
        if (method == "prompts/list") {
            if (notification) return {};
            return makeResult(id, {{"prompts", json::array()}}).dump();
        }
        if (method == "tools/call") {
            if (notification) return {};
            if (!params.contains("name") || !params["name"].is_string())
                return makeError(id, -32602, "missing tool name").dump();
            const std::string name = params["name"].get<std::string>();
            json arguments = params.value("arguments", json::object());
            if (!arguments.is_object())
                return makeResult(id, textContent(
                    "tool arguments must be an object", true)).dump();

            const ToolDef* definition = nullptr;
            for (const auto& tool : tools()) {
                if (tool.name == name) {
                    definition = &tool;
                    break;
                }
            }
            if (!definition) {
                return makeResult(
                    id, textContent("unknown tool: " + name, true)).dump();
            }

            if (definition->lua) {
#ifdef VWAVE_HAS_LUA
                if (!arguments.contains("code") ||
                    !arguments["code"].is_string()) {
                    return makeResult(
                        id, textContent("missing code", true)).dump();
                }
                ScriptEngine engine(m_session);
                std::string error;
                if (!engine.runString(arguments["code"].get<std::string>(),
                                      &error)) {
                    return makeResult(id,
                                      textContent(error, true)).dump();
                }
                return makeResult(
                    id, textContent(m_session.info().dump(), false,
                                    m_session.info()))
                    .dump();
#else
                return makeResult(
                    id, textContent("Lua support is not built", true)).dump();
#endif
            }

            std::string command = definition->command;
            json parameters = arguments;
            if (definition->generic) {
                if (!arguments.contains("command") ||
                    !arguments["command"].is_string()) {
                    return makeResult(
                        id, textContent("missing command", true)).dump();
                }
                command = arguments["command"].get<std::string>();
                parameters = arguments.value("parameters", json::object());
                if (!parameters.is_object()) {
                    return makeResult(
                        id,
                        textContent("parameters must be an object", true))
                        .dump();
                }
            }

            std::string error;
            const json result =
                m_session.execute(command, parameters, &error);
            if (result.is_null()) {
                return makeResult(
                    id, textContent(error.empty() ? "command failed" : error,
                                    true))
                    .dump();
            }
            return makeResult(
                id, textContent(result.dump(), false, result)).dump();
        }
        if (notification) return {};
        return makeError(id, -32601, "method not found: " + method).dump();
    } catch (const std::exception& e) {
        if (notification) return {};
        return makeError(id, -32603,
                         std::string("internal error: ") + e.what())
            .dump();
    }
}

void McpServer::runStdio(std::istream& input, std::ostream& output) {
    std::string line;
    while (std::getline(input, line)) {
        line = trimCarriageReturn(std::move(line));
        if (line.empty()) continue;
        std::string message;
        if (line.rfind("Content-Length:", 0) == 0) {
            size_t length = 0;
            try {
                length = static_cast<size_t>(
                    std::stoull(line.substr(std::string("Content-Length:").size())));
            } catch (...) {
                length = 0;
            }
            while (std::getline(input, line)) {
                line = trimCarriageReturn(std::move(line));
                if (line.empty()) break;
            }
            if (length > 0) {
                message.resize(length);
                input.read(message.data(), static_cast<std::streamsize>(length));
            }
        } else {
            message = line;
        }
        if (message.empty()) continue;
        const std::string response = handleMessage(message);
        if (!response.empty()) {
            output << response << '\n';
            output.flush();
        }
    }
}

std::vector<std::string> McpServer::toolNames() {
    std::vector<std::string> names;
    for (const auto& tool : tools()) names.push_back(tool.name);
    return names;
}

} // namespace vwave
