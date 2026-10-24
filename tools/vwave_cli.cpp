#include "vwave/Mcp.h"
#include "vwave/Scripting.h"
#include "vwave/Session.h"
#include "vwave/Version.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using nlohmann::json;

void printUsage() {
    std::cout <<
        "VWaveEditor " VWAVE_VERSION "\n"
        "Usage:\n"
        "  vwave info <audio>\n"
        "  vwave process <audio> <out> [options]\n"
        "  vwave analyze <audio> [--beats] [--pitch]\n"
        "  vwave slices <audio> <directory> [--mode transients|beats|markers]\n"
        "  vwave wavetable <audio> <out.vwt> [--frame-size N] [--frame-count N]\n"
        "  vwave synth <wavetable> <out.wav> [--note MIDI] [--duration SEC]\n"
        "  vwave sweep <out.wav> [--seconds SEC] [--start-hz HZ] [--end-hz HZ]\n"
        "  vwave deconvolve <recorded> <sweep> <out.wav> [--length FRAMES]\n"
        "  vwave project info <project.vwdoc>\n"
        "  vwave project add-track <project> <audio> [--name NAME] [--start SEC]\n"
        "  vwave project add-empty-track <project> [--name NAME] [--start SEC]\n"
        "  vwave project remove-track <project> <index>\n"
        "  vwave project select-track <project> <index>\n"
        "  vwave project set <project> <index> [--gain DB] [--pan V]\n"
        "      [--name NAME] [--start SEC] [--mute|--unmute] [--solo] [--arm]\n"
        "  vwave project mixdown <project> <out.wav>\n"
        "  vwave lua <script.lua> [audio]\n"
        "  vwave command <name> [json] [--input audio] [--output audio]\n"
        "  vwave --mcp\n"
        "  vwave --version\n"
        "\n"
        "Process options apply in order:\n"
        "  --select START END         selection in seconds\n"
        "  --select-all\n"
        "  --gain DB                  --normalize [DB]\n"
        "  --fade-in                  --fade-out\n"
        "  --reverse                  --invert\n"
        "  --silence                  --dc-remove\n"
        "  --copy                     --cut\n"
        "  --delete                   --crop\n"
        "  --stretch RATIO            --algorithm wsola|phase_vocoder|paulstretch\n"
        "  --pitch SEMITONES          --paulstretch RATIO\n"
        "  --resample SAMPLE_RATE\n"
        "  --spectral-gain LOW HIGH DB\n"
        "  --spectral-filter TYPE CUTOFF\n"
        "  --spectral-gate DB\n"
        "  --spectral-blur TIME FREQ\n"
        "  --format wav|aiff|flac|ogg  --bit-depth int16|int24|float32\n";
}

bool looksLikeNumber(const std::string& text) {
    if (text.empty()) return false;
    char* end = nullptr;
    std::strtod(text.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool parseInt(const std::string& text, int& value) {
    try {
        size_t used = 0;
        value = std::stoi(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

std::string valueAt(const std::vector<std::string>& args, size_t& index,
                    const std::string& option) {
    if (index + 1 >= args.size()) {
        std::cerr << "missing value for " << option << '\n';
        std::exit(2);
    }
    return args[++index];
}

void printJson(const json& value) {
    std::cout << value.dump(2) << '\n';
}

int fail(const std::string& message) {
    std::cerr << "vwave: " << message << '\n';
    return 1;
}

json executeOrFail(vwave::Session& session, const std::string& command,
                   const json& parameters = json::object()) {
    std::string error;
    const json result = session.execute(command, parameters, &error);
    if (result.is_null()) {
        std::cerr << "vwave: " << (error.empty() ? "command failed" : error)
                  << '\n';
        std::exit(1);
    }
    return result;
}

int commandInfo(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("info requires an audio file");
    vwave::Session session;
    const std::string extension =
        std::filesystem::path(args[2]).extension().string();
    if (extension == ".vwdoc")
        executeOrFail(session, "load_project", {{"path", args[2]}});
    else
        executeOrFail(session, "load_audio", {{"path", args[2]}});
    printJson(session.info());
    return 0;
}

int commandProcess(const std::vector<std::string>& args) {
    if (args.size() < 4) return fail("process requires input and output");
    vwave::Session session;
    executeOrFail(session, "load_audio", {{"path", args[2]}});

    std::string format = "auto";
    std::string bitDepth = "float32";
    for (size_t i = 4; i < args.size(); ++i) {
        const std::string& option = args[i];
        if (option == "--select") {
            const double start = std::stod(valueAt(args, i, option));
            const double end = std::stod(valueAt(args, i, option));
            executeOrFail(session, "select", {{"start", start}, {"end", end}});
        } else if (option == "--select-all") {
            executeOrFail(session, "select_all");
        } else if (option == "--gain") {
            const double db = std::stod(valueAt(args, i, option));
            executeOrFail(session, "gain", {{"db", db}});
        } else if (option == "--normalize") {
            double db = -1.0;
            if (i + 1 < args.size() && looksLikeNumber(args[i + 1])) {
                db = std::stod(valueAt(args, i, option));
            }
            executeOrFail(session, "normalize", {{"peak_db", db}});
        } else if (option == "--fade-in") {
            executeOrFail(session, "fade_in");
        } else if (option == "--fade-out") {
            executeOrFail(session, "fade_out");
        } else if (option == "--reverse") {
            executeOrFail(session, "reverse");
        } else if (option == "--invert") {
            executeOrFail(session, "invert");
        } else if (option == "--silence") {
            executeOrFail(session, "silence");
        } else if (option == "--dc-remove") {
            executeOrFail(session, "dc_remove");
        } else if (option == "--copy") {
            executeOrFail(session, "copy");
        } else if (option == "--cut") {
            executeOrFail(session, "cut");
        } else if (option == "--delete") {
            executeOrFail(session, "delete");
        } else if (option == "--crop") {
            executeOrFail(session, "crop");
        } else if (option == "--stretch") {
            const double ratio = std::stod(valueAt(args, i, option));
            std::string algorithm = "wsola";
            if (i + 1 < args.size() && args[i + 1] == "--algorithm")
                algorithm = valueAt(args, ++i, "--algorithm");
            executeOrFail(session, "time_stretch",
                          {{"ratio", ratio}, {"algorithm", algorithm}});
        } else if (option == "--pitch") {
            const double semitones = std::stod(valueAt(args, i, option));
            std::string algorithm = "phase_vocoder";
            if (i + 1 < args.size() && args[i + 1] == "--algorithm")
                algorithm = valueAt(args, ++i, "--algorithm");
            executeOrFail(session, "pitch_shift",
                          {{"semitones", semitones},
                           {"algorithm", algorithm}});
        } else if (option == "--paulstretch") {
            const double ratio = std::stod(valueAt(args, i, option));
            executeOrFail(session, "paulstretch", {{"ratio", ratio}});
        } else if (option == "--resample") {
            int rate = 0;
            if (!parseInt(valueAt(args, i, option), rate))
                return fail("invalid sample rate");
            executeOrFail(session, "resample", {{"sample_rate", rate}});
        } else if (option == "--spectral-gain") {
            const double low = std::stod(valueAt(args, i, option));
            const double high = std::stod(valueAt(args, i, option));
            const double db = std::stod(valueAt(args, i, option));
            executeOrFail(session, "spectral_gain",
                          {{"low_hz", low}, {"high_hz", high}, {"db", db}});
        } else if (option == "--spectral-filter") {
            const std::string type = valueAt(args, i, option);
            const double cutoff = std::stod(valueAt(args, i, option));
            executeOrFail(session, "spectral_filter",
                          {{"type", type}, {"cutoff_hz", cutoff}});
        } else if (option == "--spectral-gate") {
            const double db = std::stod(valueAt(args, i, option));
            executeOrFail(session, "spectral_gate", {{"threshold_db", db}});
        } else if (option == "--spectral-blur") {
            const int time = std::stoi(valueAt(args, i, option));
            const int frequency = std::stoi(valueAt(args, i, option));
            executeOrFail(session, "spectral_blur",
                          {{"time_radius", time},
                           {"frequency_radius", frequency}});
        } else if (option == "--format") {
            format = valueAt(args, i, option);
        } else if (option == "--bit-depth") {
            bitDepth = valueAt(args, i, option);
        } else {
            return fail("unknown process option: " + option);
        }
    }

    executeOrFail(session, "save_audio",
                  {{"path", args[3]}, {"format", format},
                   {"bit_depth", bitDepth}});
    std::cout << args[3] << '\n';
    return 0;
}

int commandAnalyze(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("analyze requires an audio file");
    vwave::Session session;
    executeOrFail(session, "load_audio", {{"path", args[2]}});
    bool beats = true;
    bool pitch = true;
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--beats") beats = true;
        else if (args[i] == "--pitch") pitch = true;
    }
    json result;
    if (beats) result["beats"] = executeOrFail(session, "analyze_beats");
    if (pitch) result["pitch"] = executeOrFail(session, "detect_pitch");
    printJson(result);
    return 0;
}

int commandSlices(const std::vector<std::string>& args) {
    if (args.size() < 4) return fail("slices requires an audio file and directory");
    vwave::Session session;
    executeOrFail(session, "load_audio", {{"path", args[2]}});
    std::string mode = "transients";
    for (size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--mode") mode = valueAt(args, i, "--mode");
    }
    executeOrFail(session, "slices", {{"mode", mode}});
    const json exported =
        executeOrFail(session, "export_slices", {{"directory", args[3]}});
    printJson(exported);
    return 0;
}

int commandWavetable(const std::vector<std::string>& args) {
    if (args.size() < 4) return fail("wavetable requires input and output");
    vwave::Session session;
    executeOrFail(session, "load_audio", {{"path", args[2]}});
    json parameters = {{"path", args[3]}};
    for (size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--frame-size") {
            parameters["frame_size"] = std::stoi(valueAt(args, i, "--frame-size"));
        } else if (args[i] == "--frame-count") {
            parameters["frame_count"] =
                std::stoi(valueAt(args, i, "--frame-count"));
        } else if (args[i] == "--start") {
            parameters["start"] = std::stod(valueAt(args, i, "--start"));
        } else if (args[i] == "--end") {
            parameters["end"] = std::stod(valueAt(args, i, "--end"));
        } else {
            return fail("unknown wavetable option: " + args[i]);
        }
    }
    printJson(executeOrFail(session, "wavetable", parameters));
    return 0;
}

int commandSynth(const std::vector<std::string>& args) {
    if (args.size() < 4) return fail("synth requires wavetable and output");
    vwave::Session session;
    json parameters = {{"wavetable", args[2]},
                       {"out", args[3]},
                       {"midi_note", 60.0},
                       {"duration", 1.0}};
    for (size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--note")
            parameters["midi_note"] = std::stod(valueAt(args, i, "--note"));
        else if (args[i] == "--duration")
            parameters["duration"] = std::stod(valueAt(args, i, "--duration"));
        else if (args[i] == "--channels")
            parameters["channels"] = std::stoi(valueAt(args, i, "--channels"));
        else if (args[i] == "--unison")
            parameters["unison"] = std::stoi(valueAt(args, i, "--unison"));
        else if (args[i] == "--morph")
            parameters["morph"] = std::stod(valueAt(args, i, "--morph"));
        else
            return fail("unknown synth option: " + args[i]);
    }
    printJson(executeOrFail(session, "synth_note", parameters));
    return 0;
}

int commandSweep(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("sweep requires an output file");
    vwave::Session session;
    json parameters = {{"out", args[2]},
                       {"seconds", 5.0},
                       {"start_hz", 20.0},
                       {"end_hz", 20000.0}};
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--seconds")
            parameters["seconds"] = std::stod(valueAt(args, i, "--seconds"));
        else if (args[i] == "--start-hz")
            parameters["start_hz"] = std::stod(valueAt(args, i, "--start-hz"));
        else if (args[i] == "--end-hz")
            parameters["end_hz"] = std::stod(valueAt(args, i, "--end-hz"));
        else
            return fail("unknown sweep option: " + args[i]);
    }
    printJson(executeOrFail(session, "generate_sweep", parameters));
    return 0;
}

int commandDeconvolve(const std::vector<std::string>& args) {
    if (args.size() < 5)
        return fail("deconvolve requires recorded, sweep and output");
    vwave::Session session;
    json parameters = {{"recorded", args[2]},
                       {"sweep", args[3]},
                       {"out", args[4]}};
    for (size_t i = 5; i < args.size(); ++i) {
        if (args[i] == "--length")
            parameters["ir_length"] = std::stoll(valueAt(args, i, "--length"));
        else if (args[i] == "--trim")
            parameters["trim_to_peak"] = true;
        else
            return fail("unknown deconvolve option: " + args[i]);
    }
    printJson(executeOrFail(session, "deconvolve", parameters));
    return 0;
}

int commandLua(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("lua requires a script file");
    vwave::Session session;
    if (args.size() >= 4)
        executeOrFail(session, "load_audio", {{"path", args[3]}});
    vwave::ScriptEngine engine(session);
    std::string error;
    if (!engine.runFile(args[2], &error)) return fail(error);
    return 0;
}


int commandProject(const std::vector<std::string>& args) {
    if (args.size() < 4)
        return fail("project requires a subcommand and project file");
    const std::string& subcommand = args[2];
    const std::string& projectPath = args[3];
    vwave::Session session;
    std::string error;
    if (!session.loadProject(projectPath, &error)) return fail(error);

    if (subcommand == "info") {
        printJson(session.info());
        return 0;
    }
    if (subcommand == "tracks") {
        printJson(session.execute("tracks", json::object(), &error));
        return error.empty() ? 0 : fail(error);
    }
    if (subcommand == "add-track") {
        if (args.size() < 5) return fail("add-track requires an audio file");
        json parameters = {{"path", args[4]}};
        for (size_t i = 5; i < args.size(); ++i) {
            if (args[i] == "--name")
                parameters["name"] = valueAt(args, i, "--name");
            else if (args[i] == "--start")
                parameters["start"] = std::stod(valueAt(args, i, "--start"));
            else
                return fail("unknown add-track option: " + args[i]);
        }
        executeOrFail(session, "add_track", parameters);
        executeOrFail(session, "save_project", {{"path", projectPath}});
        printJson(session.info());
        return 0;
    }
    if (subcommand == "add-empty-track") {
        json parameters = json::object();
        for (size_t i = 4; i < args.size(); ++i) {
            if (args[i] == "--name")
                parameters["name"] = valueAt(args, i, "--name");
            else if (args[i] == "--start")
                parameters["start"] = std::stod(valueAt(args, i, "--start"));
            else
                return fail("unknown add-empty-track option: " + args[i]);
        }
        executeOrFail(session, "add_empty_track", parameters);
        executeOrFail(session, "save_project", {{"path", projectPath}});
        printJson(session.info());
        return 0;
    }
    if (subcommand == "remove-track") {
        if (args.size() < 5) return fail("remove-track requires an index");
        executeOrFail(session, "remove_track", {{"index", std::stoi(args[4])}});
        executeOrFail(session, "save_project", {{"path", projectPath}});
        printJson(session.info());
        return 0;
    }
    if (subcommand == "select-track") {
        if (args.size() < 5) return fail("select-track requires an index");
        executeOrFail(session, "select_track", {{"index", std::stoi(args[4])}});
        executeOrFail(session, "save_project", {{"path", projectPath}});
        printJson(session.info());
        return 0;
    }
    if (subcommand == "set") {
        if (args.size() < 5) return fail("set requires a track index");
        json parameters = {{"index", std::stoi(args[4])}};
        for (size_t i = 5; i < args.size(); ++i) {
            if (args[i] == "--name")
                parameters["name"] = valueAt(args, i, "--name");
            else if (args[i] == "--gain")
                parameters["gain_db"] = std::stod(valueAt(args, i, "--gain"));
            else if (args[i] == "--pan")
                parameters["pan"] = std::stod(valueAt(args, i, "--pan"));
            else if (args[i] == "--start")
                parameters["start"] = std::stod(valueAt(args, i, "--start"));
            else if (args[i] == "--mute")
                parameters["muted"] = true;
            else if (args[i] == "--unmute")
                parameters["muted"] = false;
            else if (args[i] == "--solo")
                parameters["solo"] = true;
            else if (args[i] == "--unsolo")
                parameters["solo"] = false;
            else if (args[i] == "--arm")
                parameters["armed"] = true;
            else if (args[i] == "--disarm")
                parameters["armed"] = false;
            else
                return fail("unknown set option: " + args[i]);
        }
        executeOrFail(session, "track_set", parameters);
        executeOrFail(session, "save_project", {{"path", projectPath}});
        printJson(session.info());
        return 0;
    }
    if (subcommand == "mixdown") {
        if (args.size() < 5) return fail("mixdown requires an output file");
        executeOrFail(session, "save_mixdown", {{"path", args[4]}});
        std::cout << args[4] << '\n';
        return 0;
    }
    return fail("unknown project subcommand: " + subcommand);
}

int commandGeneric(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("command requires a command name");
    vwave::Session session;
    std::string input;
    std::string output;
    json parameters = json::object();
    size_t index = 3;
    if (index < args.size() && args[index] != "--input" &&
        args[index] != "--output") {
        try {
            parameters = json::parse(args[index]);
        } catch (const std::exception& e) {
            return fail(std::string("invalid JSON parameters: ") + e.what());
        }
        ++index;
    }
    for (; index < args.size(); ++index) {
        if (args[index] == "--input") input = valueAt(args, index, "--input");
        else if (args[index] == "--output") output = valueAt(args, index, "--output");
        else return fail("unknown command option: " + args[index]);
    }
    if (!input.empty())
        executeOrFail(session, "load_audio", {{"path", input}});
    const json result = executeOrFail(session, args[2], parameters);
    if (!output.empty())
        executeOrFail(session, "save_audio", {{"path", output}});
    printJson(result);
    return 0;
}

int commandMcp() {
    vwave::Session session;
    vwave::McpServer server(session);
    server.runStdio(std::cin, std::cout);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    if (args.size() < 2) {
        printUsage();
        return 0;
    }
    if (args[1] == "--version" || args[1] == "-v") {
        std::cout << "VWaveEditor " VWAVE_VERSION << '\n';
        return 0;
    }
    if (args[1] == "--help" || args[1] == "-h") {
        printUsage();
        return 0;
    }
    if (args[1] == "--mcp" || args[1] == "mcp") return commandMcp();
    if (args[1] == "info") return commandInfo(args);
    if (args[1] == "process") return commandProcess(args);
    if (args[1] == "analyze") return commandAnalyze(args);
    if (args[1] == "slices") return commandSlices(args);
    if (args[1] == "wavetable") return commandWavetable(args);
    if (args[1] == "synth") return commandSynth(args);
    if (args[1] == "sweep") return commandSweep(args);
    if (args[1] == "deconvolve") return commandDeconvolve(args);
    if (args[1] == "project") return commandProject(args);
    if (args[1] == "lua") return commandLua(args);
    if (args[1] == "command") return commandGeneric(args);
    std::cerr << "vwave: unknown command: " << args[1] << '\n';
    printUsage();
    return 2;
}
