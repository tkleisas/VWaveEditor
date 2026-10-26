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
        "  vwave mastering <project.vwdoc> [--profile NAME] [--out report.json]\n"
        "  vwave mastering-render <project.vwdoc> [--out-dir DIR] [--add-tracks]\n"
        "  vwave image <image> <out.wav> [--duration SEC] [--phase MODE]\n"
        "  vwave scope <out.wav> [--shape NAME] [--scan-hz HZ] [--duration SEC]\n"
        "  vwave slices <audio> <directory> [--mode transients|beats|markers]\n"
        "  vwave wavetable <audio> <out.vwt> [--frame-size N] [--frame-count N]\n"
        "      [--morph linear|equal_power|phase_aligned|spectral|harmonic]\n"
        "      [--root MIDI] [--oversample N] [--band-limit] [--quantize BITS]\n"
        "      [--align-phases] [--no-normalize] [--no-dc] [--no-phase-align]\n"
        "  vwave wavetable-design <spec.json> <out.vwt>\n"
        "  vwave synth <wavetable> <out.wav> [--note MIDI] [--duration SEC]\n"
        "      [--morph VALUE] [--blend W1,W2,...] [--oversample N] [--use-table-root]\n"
        "  vwave sweep <out.wav> [--seconds SEC] [--start-hz HZ] [--end-hz HZ]\n"
        "  vwave deconvolve <recorded> <sweep> <out.wav> [--length FRAMES]\n"
        "  vwave project info <project.vwdoc>\n"
        "  vwave project add-track <project> <audio> [--name NAME] [--start SEC]\n"
        "  vwave project add-empty-track <project> [--name NAME] [--start SEC]\n"
        "  vwave project remove-track <project> <index>\n"
        "  vwave project select-track <project> <index>\n"
        "  vwave project set <project> <index> [--gain DB] [--pan V]\n"
        "      [--name NAME] [--start SEC] [--mute|--unmute] [--solo] [--arm]\n"
        "  vwave project regenerate <project> [index]\n"
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

int commandMastering(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("mastering requires a project file");
    vwave::Session session;
    executeOrFail(session, "load_project", {{"path", args[2]}});
    json parameters = {{"profile", "balanced"}};
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--profile") {
            parameters["profile"] = valueAt(args, i, "--profile");
        } else if (args[i] == "--out") {
            parameters["out"] = valueAt(args, i, "--out");
        } else if (args[i] == "--band-count") {
            parameters["band_count"] =
                std::stoi(valueAt(args, i, "--band-count"));
        } else if (args[i] == "--fft-size") {
            parameters["fft_size"] =
                std::stoi(valueAt(args, i, "--fft-size"));
        } else if (args[i] == "--hop") {
            parameters["hop_size"] = std::stoi(valueAt(args, i, "--hop"));
        } else if (args[i] == "--max-cut") {
            parameters["max_cut_db"] =
                std::stod(valueAt(args, i, "--max-cut"));
        } else if (args[i] == "--max-boost") {
            parameters["max_boost_db"] =
                std::stod(valueAt(args, i, "--max-boost"));
        } else if (args[i] == "--smoothing") {
            parameters["smoothing_bands"] =
                std::stoi(valueAt(args, i, "--smoothing"));
        } else if (args[i] == "--include-muted") {
            parameters["include_muted"] = true;
        } else {
            return fail("unknown mastering option: " + args[i]);
        }
    }
    printJson(executeOrFail(session, "mastering_analysis", parameters));
    return 0;
}

int commandMasteringRender(const std::vector<std::string>& args) {
    if (args.size() < 3)
        return fail("mastering-render requires a project file");
    vwave::Session session;
    executeOrFail(session, "load_project", {{"path", args[2]}});
    json parameters = {{"profile", "balanced"}};
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--profile") {
            parameters["profile"] = valueAt(args, i, "--profile");
        } else if (args[i] == "--out-dir") {
            parameters["out_dir"] = valueAt(args, i, "--out-dir");
        } else if (args[i] == "--prefix") {
            parameters["prefix"] = valueAt(args, i, "--prefix");
        } else if (args[i] == "--add-tracks") {
            parameters["add_tracks"] = true;
        } else if (args[i] == "--mute-originals") {
            parameters["mute_originals"] = true;
        } else if (args[i] == "--ceiling") {
            parameters["ceiling_db"] =
                std::stod(valueAt(args, i, "--ceiling"));
        } else if (args[i] == "--band-count") {
            parameters["band_count"] =
                std::stoi(valueAt(args, i, "--band-count"));
        } else if (args[i] == "--fft-size") {
            parameters["fft_size"] =
                std::stoi(valueAt(args, i, "--fft-size"));
        } else if (args[i] == "--hop") {
            parameters["hop_size"] = std::stoi(valueAt(args, i, "--hop"));
        } else if (args[i] == "--process-fft-size") {
            parameters["process_fft_size"] =
                std::stoi(valueAt(args, i, "--process-fft-size"));
        } else if (args[i] == "--process-hop") {
            parameters["process_hop_size"] =
                std::stoi(valueAt(args, i, "--process-hop"));
        } else if (args[i] == "--max-cut") {
            parameters["max_cut_db"] =
                std::stod(valueAt(args, i, "--max-cut"));
        } else if (args[i] == "--max-boost") {
            parameters["max_boost_db"] =
                std::stod(valueAt(args, i, "--max-boost"));
        } else if (args[i] == "--smoothing") {
            parameters["smoothing_bands"] =
                std::stoi(valueAt(args, i, "--smoothing"));
        } else if (args[i] == "--include-muted") {
            parameters["include_muted"] = true;
        } else if (args[i] == "--no-limit") {
            parameters["limit_mix"] = false;
        } else {
            return fail("unknown mastering-render option: " + args[i]);
        }
    }
    printJson(executeOrFail(session, "mastering_render", parameters));
    return 0;
}

int commandImage(const std::vector<std::string>& args) {
    if (args.size() < 4) return fail("image requires an image and output");
    vwave::Session session;
    json parameters = {{"path", args[2]}, {"out", args[3]}};
    for (size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--duration") {
            parameters["duration"] =
                std::stod(valueAt(args, i, "--duration"));
        } else if (args[i] == "--fft-size") {
            parameters["fft_size"] =
                std::stoi(valueAt(args, i, "--fft-size"));
        } else if (args[i] == "--hop") {
            parameters["hop_size"] = std::stoi(valueAt(args, i, "--hop"));
        } else if (args[i] == "--sample-rate") {
            parameters["sample_rate"] =
                std::stoi(valueAt(args, i, "--sample-rate"));
        } else if (args[i] == "--min-hz") {
            parameters["min_hz"] = std::stod(valueAt(args, i, "--min-hz"));
        } else if (args[i] == "--max-hz") {
            parameters["max_hz"] = std::stod(valueAt(args, i, "--max-hz"));
        } else if (args[i] == "--freq-scale") {
            parameters["frequency_scale"] =
                valueAt(args, i, "--freq-scale");
        } else if (args[i] == "--time-map") {
            parameters["time_mapping"] =
                valueAt(args, i, "--time-map");
        } else if (args[i] == "--loops") {
            parameters["loops"] = std::stoi(valueAt(args, i, "--loops"));
        } else if (args[i] == "--gamma") {
            parameters["gamma"] = std::stod(valueAt(args, i, "--gamma"));
        } else if (args[i] == "--contrast") {
            parameters["contrast"] =
                std::stod(valueAt(args, i, "--contrast"));
        } else if (args[i] == "--threshold") {
            parameters["threshold"] =
                std::stod(valueAt(args, i, "--threshold"));
        } else if (args[i] == "--invert") {
            parameters["invert"] = true;
        } else if (args[i] == "--no-alpha") {
            parameters["use_alpha"] = false;
        } else if (args[i] == "--phase") {
            parameters["phase_mode"] = valueAt(args, i, "--phase");
        } else if (args[i] == "--iterations") {
            parameters["iterations"] =
                std::stoi(valueAt(args, i, "--iterations"));
        } else if (args[i] == "--seed") {
            parameters["seed"] = std::stoi(valueAt(args, i, "--seed"));
        } else if (args[i] == "--channels") {
            parameters["channels"] =
                std::stoi(valueAt(args, i, "--channels"));
        } else if (args[i] == "--no-normalize") {
            parameters["normalize"] = false;
        } else if (args[i] == "--peak") {
            parameters["peak"] = std::stod(valueAt(args, i, "--peak"));
        } else {
            return fail("unknown image option: " + args[i]);
        }
    }
    printJson(executeOrFail(session, "image_spectrogram", parameters));
    return 0;
}

int commandScope(const std::vector<std::string>& args) {
    if (args.size() < 3) return fail("scope requires an output file");
    vwave::Session session;
    json parameters = {{"out", args[2]}, {"shape", "circle"}};
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--shape") {
            parameters["shape"] = valueAt(args, i, "--shape");
        } else if (args[i] == "--path-file") {
            parameters["path_file"] = valueAt(args, i, "--path-file");
        } else if (args[i] == "--point-count") {
            parameters["point_count"] =
                std::stoi(valueAt(args, i, "--point-count"));
        } else if (args[i] == "--size") {
            parameters["size"] = std::stod(valueAt(args, i, "--size"));
        } else if (args[i] == "--a") {
            parameters["a"] = std::stod(valueAt(args, i, "--a"));
        } else if (args[i] == "--b") {
            parameters["b"] = std::stod(valueAt(args, i, "--b"));
        } else if (args[i] == "--phase") {
            parameters["phase"] = std::stod(valueAt(args, i, "--phase"));
        } else if (args[i] == "--turns") {
            parameters["turns"] = std::stoi(valueAt(args, i, "--turns"));
        } else if (args[i] == "--name") {
            parameters["name"] = valueAt(args, i, "--name");
        } else if (args[i] == "--duration") {
            parameters["duration"] =
                std::stod(valueAt(args, i, "--duration"));
        } else if (args[i] == "--scan-hz") {
            parameters["scan_hz"] = std::stod(valueAt(args, i, "--scan-hz"));
        } else if (args[i] == "--sample-rate") {
            parameters["sample_rate"] =
                std::stoi(valueAt(args, i, "--sample-rate"));
        } else if (args[i] == "--gain") {
            parameters["gain"] = std::stod(valueAt(args, i, "--gain"));
        } else if (args[i] == "--smoothing") {
            parameters["smoothing"] =
                std::stod(valueAt(args, i, "--smoothing"));
        } else if (args[i] == "--raw") {
            parameters["band_limited"] = false;
        } else if (args[i] == "--band-limit-hz") {
            parameters["band_limit_hz"] =
                std::stod(valueAt(args, i, "--band-limit-hz"));
        } else if (args[i] == "--oversample") {
            parameters["oversampling"] =
                std::stoi(valueAt(args, i, "--oversample"));
        } else if (args[i] == "--direction") {
            parameters["direction"] = valueAt(args, i, "--direction");
        } else if (args[i] == "--no-normalize") {
            parameters["normalize"] = false;
        } else if (args[i] == "--peak") {
            parameters["peak"] = std::stod(valueAt(args, i, "--peak"));
        } else if (args[i] == "--save-path") {
            parameters["save_path"] = valueAt(args, i, "--save-path");
        } else {
            return fail("unknown scope option: " + args[i]);
        }
    }
    printJson(executeOrFail(session, "oscilloscope_render", parameters));
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
        } else if (args[i] == "--morph") {
            parameters["morph_mode"] = valueAt(args, i, "--morph");
        } else if (args[i] == "--interpolation") {
            parameters["interpolation"] =
                valueAt(args, i, "--interpolation");
        } else if (args[i] == "--root") {
            parameters["root_note"] = std::stod(valueAt(args, i, "--root"));
        } else if (args[i] == "--oversample") {
            parameters["oversampling"] =
                std::stoi(valueAt(args, i, "--oversample"));
        } else if (args[i] == "--band-limit") {
            parameters["band_limit"] = true;
        } else if (args[i] == "--max-harmonic") {
            parameters["max_harmonic"] =
                std::stoi(valueAt(args, i, "--max-harmonic"));
        } else if (args[i] == "--quantize") {
            parameters["quantize_bits"] =
                std::stoi(valueAt(args, i, "--quantize"));
        } else if (args[i] == "--align-phases") {
            parameters["align_frames"] = true;
        } else if (args[i] == "--no-normalize") {
            parameters["normalize"] = false;
        } else if (args[i] == "--no-dc") {
            parameters["remove_dc"] = false;
        } else if (args[i] == "--no-phase-align") {
            parameters["phase_align"] = false;
        } else {
            return fail("unknown wavetable option: " + args[i]);
        }
    }
    printJson(executeOrFail(session, "wavetable", parameters));
    return 0;
}

int commandWavetableDesign(const std::vector<std::string>& args) {
    if (args.size() < 4)
        return fail("wavetable-design requires a spec and output");
    std::ifstream in(args[2], std::ios::binary);
    if (!in) return fail("cannot open wavetable spec: " + args[2]);
    json parameters;
    try {
        in >> parameters;
    } catch (const std::exception& error) {
        return fail(std::string("invalid wavetable spec: ") + error.what());
    }
    if (!parameters.is_object())
        return fail("wavetable spec must be a JSON object");
    parameters["path"] = args[3];
    vwave::Session session;
    printJson(executeOrFail(session, "wavetable_design", parameters));
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
        else if (args[i] == "--oversample")
            parameters["oversampling"] =
                std::stoi(valueAt(args, i, "--oversample"));
        else if (args[i] == "--use-table-root")
            parameters["use_table_root"] = true;
        else if (args[i] == "--blend") {
            const std::string list = valueAt(args, i, "--blend");
            json weights = json::array();
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const std::string item = list.substr(
                    start, comma == std::string::npos
                               ? std::string::npos
                               : comma - start);
                if (item.empty()) return fail("invalid --blend list");
                weights.push_back(std::stod(item));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            parameters["blend_weights"] = std::move(weights);
        } else
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
    if (subcommand == "regenerate") {
        json parameters = json::object();
        if (args.size() >= 5) parameters["index"] = std::stoi(args[4]);
        executeOrFail(session, "regenerate_track", parameters);
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
    if (args[1] == "mastering") return commandMastering(args);
    if (args[1] == "mastering-render")
        return commandMasteringRender(args);
    if (args[1] == "image") return commandImage(args);
    if (args[1] == "scope") return commandScope(args);
    if (args[1] == "wavetable") return commandWavetable(args);
    if (args[1] == "wavetable-design") return commandWavetableDesign(args);
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
