#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/AudioIO.h"
#include "vwave/Processing.h"
#include "vwave/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace vwave;
using nlohmann::json;

namespace {

double kPi = 3.14159265358979323846;

Audio tone(double frequency, double seconds, int sampleRate = 48000,
           int channels = 2) {
    const int64_t frames = static_cast<int64_t>(seconds * sampleRate);
    Audio audio(channels, frames, sampleRate);
    for (int64_t i = 0; i < frames; ++i) {
        const double value = 0.5 * std::sin(
            2.0 * kPi * frequency * static_cast<double>(i) / sampleRate);
        for (int ch = 0; ch < channels; ++ch)
            audio.sample(ch, i) = static_cast<float>(
                ch == 0 ? value : -value);
    }
    return audio;
}

Audio clicks(double bpm, double seconds) {
    const int sampleRate = 48000;
    Audio audio(1, static_cast<int64_t>(seconds * sampleRate), sampleRate);
    const double interval = 60.0 / bpm;
    for (double time = 0.0; time < seconds; time += interval) {
        const int64_t start = static_cast<int64_t>(time * sampleRate);
        for (int64_t i = 0; i < 480; ++i) {
            const int64_t index = start + i;
            if (index >= audio.numFrames()) break;
            audio.sample(0, index) += static_cast<float>(
                std::exp(-static_cast<double>(i) / 80.0) * 0.8);
        }
    }
    return audio;
}

std::string writeTempWave(const TempDirectory& directory,
                          const std::string& name, const Audio& audio) {
    const std::string path = (directory.path() / name).string();
    std::string error;
    REQUIRE(writeAudio(path, audio, {}, &error));
    return path;
}

json makeParams(std::initializer_list<std::pair<const char*, json>> values) {
    json result = json::object();
    for (const auto& value : values) result[value.first] = value.second;
    return result;
}

} // namespace

VWTEST("session loads audio and reports info") {
    TempDirectory directory;
    const Audio source = tone(440.0, 0.1);
    const std::string path = writeTempWave(directory, "source.wav", source);
    Session session;
    std::string error;
    const json loaded = session.execute("load_audio", {{"path", path}}, &error);
    REQUIRE(!loaded.is_null());
    CHECK(error.empty());
    CHECK_EQ(session.document().numFrames(), int64_t{4800});
    CHECK_EQ(session.document().numChannels(), 2);

    const json info = session.execute("info", json::object(), &error);
    CHECK_EQ(info["sample_rate"], 48000);
    CHECK_EQ(info["channels"], 2);
    CHECK_EQ(info["frames"], 4800);
    CHECK_NEAR(info["duration"].get<double>(), 0.1, 1e-9);
    CHECK_EQ(info["source_path"], path);
    CHECK_EQ(info["undo_count"], 0);
}

VWTEST("session selection gain undo and redo") {
    TempDirectory directory;
    const Audio source = tone(440.0, 0.1);
    Session session(Document::fromAudio(source));
    std::string error;
    REQUIRE(!session.execute("select", makeParams({{"start", 0.02},
                                                    {"end", 0.04}}),
                             &error)
                 .is_null());
    const Selection selection = session.document().selection();
    CHECK_EQ(selection.start, int64_t{960});
    CHECK_EQ(selection.end, int64_t{1920});

    REQUIRE(!session.execute("gain", {{"db", -6.0}}, &error).is_null());
    CHECK_NEAR(session.document().audio().sample(0, 1000),
               source.sample(0, 1000) * 0.501187, 1e-5);
    CHECK_NEAR(session.document().audio().sample(0, 100),
               source.sample(0, 100), 1e-9);
    const json info = session.info();
    CHECK_EQ(info["undo_count"], 1);

    REQUIRE(!session.execute("undo", json::object(), &error).is_null());
    CHECK(session.document().audio().equals(source));
    REQUIRE(!session.execute("redo", json::object(), &error).is_null());
    CHECK_NEAR(session.document().audio().sample(0, 1000),
               source.sample(0, 1000) * 0.501187, 1e-5);
}

VWTEST("session processing range overrides selection") {
    const Audio source = tone(440.0, 0.05);
    Session session(Document::fromAudio(source));
    std::string error;
    const json result = session.execute(
        "gain", makeParams({{"db", -6.0}, {"start", 0.01}, {"end", 0.02}}),
        &error);
    REQUIRE(!result.is_null());
    CHECK_NEAR(session.document().audio().sample(0, 600),
               source.sample(0, 600) * 0.501187, 1e-5);
    CHECK_NEAR(session.document().audio().sample(0, 100),
               source.sample(0, 100), 1e-9);
    CHECK(session.document().selection().active);
}

VWTEST("session save audio and project round-trip") {
    TempDirectory directory;
    const Audio source = tone(330.0, 0.05);
    Session session(Document::fromAudio(source));
    std::string error;
    REQUIRE(!session.execute("select", {{"start", 0.0}, {"end", 0.01}},
                             &error)
                 .is_null());
    REQUIRE(!session.execute("reverse", json::object(), &error).is_null());
    const std::string wave = (directory.path() / "out.wav").string();
    REQUIRE(!session.execute("save_audio", {{"path", wave}}, &error).is_null());

    Audio exported;
    REQUIRE(readAudio(wave, exported));
    CHECK(exported.numFrames() == session.document().numFrames());
    CHECK_NEAR(exported.sample(0, 100), session.document().audio().sample(0, 100),
               1e-6);

    const std::string project = (directory.path() / "out.vwdoc").string();
    REQUIRE(!session.execute("save_project", {{"path", project}}, &error)
                 .is_null());
    Session loaded;
    REQUIRE(!loaded.execute("load_project", {{"path", project}}, &error)
                 .is_null());
    CHECK(loaded.document().audio().equals(session.document().audio()));
    CHECK_EQ(loaded.document().undoCount(), size_t{1});
    CHECK(loaded.document().selection().active);
}

VWTEST("session time pitch and spectral commands") {
    const Audio source = tone(440.0, 0.1);
    Session session(Document::fromAudio(source));
    std::string error;
    REQUIRE(!session.execute("select", {{"start", 0.02}, {"end", 0.04}},
                             &error)
                 .is_null());
    REQUIRE(!session.execute("time_stretch", {{"ratio", 2.0},
                                              {"algorithm", "wsola"}},
                             &error)
                 .is_null());
    CHECK_EQ(session.document().numFrames(), int64_t{5760});

    REQUIRE(!session.execute("undo", json::object(), &error).is_null());
    REQUIRE(!session.execute("select", {{"start", 0.02}, {"end", 0.04}},
                             &error)
                 .is_null());
    REQUIRE(!session.execute("pitch_shift", {{"semitones", 12.0},
                                             {"algorithm", "phase_vocoder"}},
                             &error)
                 .is_null());
    CHECK_EQ(session.document().numFrames(), source.numFrames());

    REQUIRE(!session.execute("undo", json::object(), &error).is_null());
    REQUIRE(!session.execute("spectral_gain",
                             {{"low_hz", 300.0},
                              {"high_hz", 600.0},
                              {"db", -12.0}},
                             &error)
                 .is_null());
    CHECK_EQ(session.document().numFrames(), source.numFrames());

    REQUIRE(!session.execute("undo", json::object(), &error).is_null());
    REQUIRE(!session.execute("resample", {{"sample_rate", 24000}}, &error)
                 .is_null());
    CHECK_EQ(session.document().sampleRate(), 24000);
}

VWTEST("session beat analysis and slicing") {
    TempDirectory directory;
    const std::string path =
        writeTempWave(directory, "clicks.wav", clicks(120.0, 8.0));
    Session session;
    std::string error;
    REQUIRE(!session.execute("load_audio", {{"path", path}}, &error).is_null());
    const json analysis = session.execute("analyze_beats", json::object(),
                                          &error);
    REQUIRE(!analysis.is_null());
    CHECK_NEAR(analysis["bpm"].get<double>(), 120.0, 1.5);
    CHECK(analysis["beats"].size() >= 14);

    const json sliced = session.execute(
        "slices", {{"mode", "transients"}, {"sensitivity", 0.5}}, &error);
    REQUIRE(!sliced.is_null());
    CHECK(sliced["slices"].size() >= 8);
    CHECK(session.document().slices().size() >= 8);

    const std::string exportDirectory =
        (directory.path() / "slices").string();
    const json exported = session.execute(
        "export_slices",
        {{"directory", exportDirectory}, {"prefix", "click"}}, &error);
    REQUIRE(!exported.is_null());
    CHECK(exported["files"].size() == session.document().slices().size());
    CHECK(std::filesystem::exists(exported["manifest"].get<std::string>()));
}

VWTEST("session wavetable extraction and synthesis") {
    TempDirectory directory;
    const std::string wave =
        writeTempWave(directory, "sine.wav", tone(440.0, 0.5, 48000, 1));
    Session session;
    std::string error;
    REQUIRE(!session.execute("load_audio", {{"path", wave}}, &error).is_null());
    const std::string tablePath =
        (directory.path() / "sine.vwt").string();
    const json table = session.execute(
        "wavetable",
        {{"path", tablePath}, {"frame_size", 1024}, {"frame_count", 4}},
        &error);
    REQUIRE(!table.is_null());
    CHECK_EQ(table["frame_size"], 1024);
    CHECK_EQ(table["frame_count"], 4);
    CHECK(std::filesystem::exists(tablePath));

    const std::string notePath =
        (directory.path() / "note.wav").string();
    const json note = session.execute(
        "synth_note",
        {{"wavetable", tablePath},
         {"midi_note", 69.0},
         {"duration", 0.1},
         {"out", notePath}},
        &error);
    REQUIRE(!note.is_null());
    CHECK(std::filesystem::exists(notePath));
    Audio rendered;
    REQUIRE(readAudio(notePath, rendered));
    CHECK_EQ(rendered.numFrames(), int64_t{4800});
    CHECK(rendered.peak() > 0.1f);
}

VWTEST("session sweep and deconvolution commands") {
    TempDirectory directory;
    Session session;
    std::string error;
    const std::string sweepPath = (directory.path() / "sweep.wav").string();
    const json sweep = session.execute(
        "generate_sweep",
        {{"out", sweepPath},
         {"seconds", 0.05},
         {"start_hz", 100.0},
         {"end_hz", 20000.0}},
        &error);
    REQUIRE(!sweep.is_null());
    CHECK(std::filesystem::exists(sweepPath));

    Audio sweepAudio;
    REQUIRE(readAudio(sweepPath, sweepAudio));
    Audio ir(1, 32, 48000);
    ir.sample(0, 4) = 0.5f;
    const Audio recorded = convolve(sweepAudio, ir, false);
    const std::string recordedPath =
        (directory.path() / "recorded.wav").string();
    REQUIRE(writeAudio(recordedPath, recorded, {}, &error));
    const std::string irPath = (directory.path() / "ir.wav").string();
    const json response = session.execute(
        "deconvolve",
        {{"recorded", recordedPath},
         {"sweep", sweepPath},
         {"out", irPath},
         {"ir_length", 64}},
        &error);
    REQUIRE(!response.is_null());
    CHECK(std::filesystem::exists(irPath));
    Audio recovered;
    REQUIRE(readAudio(irPath, recovered));
    CHECK_EQ(recovered.numFrames(), int64_t{64});
}

VWTEST("session markers are addressable") {
    Session session(Document::fromAudio(tone(220.0, 0.1)));
    std::string error;
    REQUIRE(!session.execute("add_marker",
                             {{"time", 0.02}, {"name", "first"}}, &error)
                 .is_null());
    REQUIRE(!session.execute("add_marker", {{"time", 0.05}}, &error).is_null());
    json info = session.info();
    CHECK_EQ(info["markers"].size(), size_t{2});
    CHECK_EQ(info["markers"][0]["name"], std::string("first"));
    REQUIRE(!session.execute("remove_marker", {{"index", 0}}, &error).is_null());
    info = session.info();
    CHECK_EQ(info["markers"].size(), size_t{1});
    REQUIRE(!session.execute("clear_markers", json::object(), &error).is_null());
    CHECK(session.document().markers().empty());
}

VWTEST("session errors do not mutate the document") {
    Session session(Document::fromAudio(tone(220.0, 0.1)));
    const Audio before = session.document().audio();
    std::string error;
    CHECK(session.execute("no_such_command", json::object(), &error).is_null());
    CHECK(!error.empty());
    error.clear();
    CHECK(session.execute("load_audio", {{"path", "/no/such/file.wav"}},
                          &error)
              .is_null());
    CHECK(!error.empty());
    CHECK(session.document().audio().equals(before));

    error.clear();
    CHECK(session.execute("select", {{"start", 0.2}, {"end", 0.1}}, &error)
              .is_null());
    CHECK(!error.empty());
    CHECK_EQ(session.document().selection().start, int64_t{0});

    error.clear();
    CHECK(session.execute("gain", {{"db", 3.0}, {"start", -1.0},
                                   {"end", 0.1}},
                          &error)
              .is_null());
    CHECK(!error.empty());
    CHECK(session.document().audio().equals(before));
}


VWTEST("session track commands add select configure and remove") {
    TempDirectory directory;
    const std::string first =
        writeTempWave(directory, "first.wav", tone(220.0, 0.5));
    const std::string second =
        writeTempWave(directory, "second.wav", tone(330.0, 0.25));
    Session session;
    std::string error;
    REQUIRE(!session.execute("load_audio", {{"path", first}}, &error).is_null());
    json info = session.info();
    CHECK_EQ(info["track_count"], 1);
    CHECK_EQ(info["active_track"], 0);
    CHECK_EQ(info["tracks"].size(), size_t{1});
    CHECK_EQ(info["tracks"][0]["name"], std::string("first"));

    REQUIRE(!session.execute("add_track",
                             {{"path", second},
                              {"name", "second"},
                              {"start", 0.25}},
                             &error)
                 .is_null());
    info = session.info();
    CHECK_EQ(info["track_count"], 2);
    CHECK_EQ(info["active_track"], 1);
    CHECK_EQ(info["tracks"][1]["name"], std::string("second"));
    CHECK_EQ(info["tracks"][1]["start_frame"], 12000);
    CHECK_NEAR(info["tracks"][1]["start_seconds"].get<double>(), 0.25, 1e-9);
    CHECK_EQ(info["project_frames"], 24000);

    REQUIRE(!session.execute("track_set",
                             {{"index", 1},
                              {"name", "Bass"},
                              {"gain_db", -6.0},
                              {"pan", 0.25},
                              {"muted", true},
                              {"solo", false},
                              {"armed", true}},
                             &error)
                 .is_null());
    const auto& track = session.timeline().track(1);
    CHECK_EQ(track.name, std::string("Bass"));
    CHECK_NEAR(track.gainDb, -6.0, 1e-9);
    CHECK_NEAR(track.pan, 0.25, 1e-9);
    CHECK(track.muted);
    CHECK(track.armed);

    REQUIRE(!session.execute("select_track", {{"index", 0}}, &error).is_null());
    info = session.info();
    CHECK_EQ(info["active_track"], 0);
    REQUIRE(!session.execute("remove_track", {{"index", 0}}, &error).is_null());
    info = session.info();
    CHECK_EQ(info["track_count"], 1);
    CHECK_EQ(session.timeline().track(0).name, std::string("Bass"));
}

VWTEST("session track edits target only the active track") {
    TempDirectory directory;
    const Audio firstAudio = tone(220.0, 0.1);
    const Audio secondAudio = tone(660.0, 0.1);
    const std::string first = writeTempWave(directory, "first.wav", firstAudio);
    const std::string second =
        writeTempWave(directory, "second.wav", secondAudio);
    Session session;
    std::string error;
    REQUIRE(!session.execute("load_audio", {{"path", first}}, &error).is_null());
    REQUIRE(!session.execute("add_track", {{"path", second}}, &error).is_null());
    REQUIRE(!session.execute("select", {{"start", 0.0}, {"end", 0.02}},
                             &error)
                 .is_null());
    REQUIRE(!session.execute("gain", {{"db", -6.0}}, &error).is_null());
    const auto& edited = session.timeline().track(1).document.audio();
    const auto& untouched = session.timeline().track(0).document.audio();
    CHECK_NEAR(edited.sample(0, 500), secondAudio.sample(0, 500) * 0.501187,
               1e-5);
    CHECK_NEAR(untouched.sample(0, 500), firstAudio.sample(0, 500), 1e-9);
}

VWTEST("session mixdown exports all audible tracks") {
    TempDirectory directory;
    const std::string first =
        writeTempWave(directory, "first.wav", tone(220.0, 0.25));
    const std::string second =
        writeTempWave(directory, "second.wav", tone(330.0, 0.25));
    Session session;
    std::string error;
    REQUIRE(!session.execute("load_audio", {{"path", first}}, &error).is_null());
    REQUIRE(!session.execute("add_track",
                             {{"path", second}, {"start", 0.1}}, &error)
                 .is_null());
    const std::string out = (directory.path() / "mix.wav").string();
    REQUIRE(!session.execute("save_mixdown", {{"path", out}}, &error).is_null());
    Audio mix;
    REQUIRE(readAudio(out, mix));
    CHECK_EQ(mix.numChannels(), 2);
    CHECK_EQ(mix.numFrames(), int64_t{16800});
    CHECK(!mix.empty());

    REQUIRE(!session.execute("track_set", {{"index", 1}, {"muted", true}},
                             &error)
                 .is_null());
    const std::string mutedOut = (directory.path() / "muted.wav").string();
    REQUIRE(!session.execute("save_mixdown", {{"path", mutedOut}}, &error)
                 .is_null());
    Audio muted;
    REQUIRE(readAudio(mutedOut, muted));
    CHECK(!muted.empty());
}

VWTEST("session timeline project preserves tracks and routing") {
    TempDirectory directory;
    const std::string first =
        writeTempWave(directory, "first.wav", tone(220.0, 0.2));
    const std::string second =
        writeTempWave(directory, "second.wav", tone(330.0, 0.2));
    Session session;
    std::string error;
    REQUIRE(!session.execute("load_audio", {{"path", first}}, &error).is_null());
    REQUIRE(!session.execute("add_track",
                             {{"path", second}, {"start", 0.1}}, &error)
                 .is_null());
    REQUIRE(!session.execute("track_set",
                             {{"index", 1},
                              {"gain_db", -3.0},
                              {"pan", -0.2},
                              {"muted", true}},
                             &error)
                 .is_null());
    const std::string project = (directory.path() / "tracks.vwdoc").string();
    REQUIRE(!session.execute("save_project", {{"path", project}}, &error)
                 .is_null());
    Session loaded;
    REQUIRE(!loaded.execute("load_project", {{"path", project}}, &error)
                 .is_null());
    const json info = loaded.info();
    CHECK_EQ(info["track_count"], 2);
    CHECK_EQ(info["active_track"], 1);
    CHECK_EQ(info["tracks"][1]["start_frame"], 4800);
    CHECK_NEAR(info["tracks"][1]["gain_db"].get<double>(), -3.0, 1e-9);
    CHECK_NEAR(info["tracks"][1]["pan"].get<double>(), -0.2, 1e-9);
    CHECK(info["tracks"][1]["muted"].get<bool>());
}

VWTEST("session track commands reject invalid indices") {
    Session session(Document::fromAudio(tone(220.0, 0.1)));
    std::string error;
    CHECK(session.execute("select_track", {{"index", 4}}, &error).is_null());
    CHECK(!error.empty());
    error.clear();
    CHECK(session.execute("remove_track", {{"index", 4}}, &error).is_null());
    CHECK(!error.empty());
    error.clear();
    CHECK(session.execute("track_set", {{"index", 4}, {"gain_db", -3.0}},
                          &error)
              .is_null());
    CHECK(!error.empty());
}

VWTEST("session command list is stable") {
    const std::vector<std::string> commands = Session::commandNames();
    CHECK(std::find(commands.begin(), commands.end(), "load_audio") !=
          commands.end());
    CHECK(std::find(commands.begin(), commands.end(), "analyze_beats") !=
          commands.end());
    CHECK(std::find(commands.begin(), commands.end(), "wavetable") !=
          commands.end());
    CHECK(std::find(commands.begin(), commands.end(), "add_track") !=
          commands.end());
    CHECK(std::find(commands.begin(), commands.end(), "track_set") !=
          commands.end());
    CHECK(std::find(commands.begin(), commands.end(), "save_mixdown") !=
          commands.end());
}
