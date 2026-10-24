#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Scripting.h"
#include "vwave/Session.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace vwave;

namespace {

Audio testTone() {
    Audio audio(1, 4800, 48000);
    for (int64_t i = 0; i < audio.numFrames(); ++i) {
        audio.sample(0, i) = static_cast<float>(
            0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 *
                           static_cast<double>(i) / 48000.0));
    }
    return audio;
}

} // namespace

VWTEST("lua can query and edit the document") {
    Session session(Document::fromAudio(testTone()));
    ScriptEngine engine(session);
    std::string error;
    const char* script = R"LUA(
vwave.select(0.01, 0.02)
vwave.gain(-6.0)
local info = vwave.info()
assert(info.channels == 1)
assert(info.undo_count == 1)
vwave.undo()
assert(vwave.info().undo_count == 0)
vwave.apply("reverse", {})
assert(vwave.info().undo_count == 1)
)LUA";
    REQUIRE(engine.runString(script, &error));
    CHECK(error.empty());
    CHECK_EQ(session.document().undoCount(), size_t{1});
}

VWTEST("lua reports script errors without corrupting the document") {
    Session session(Document::fromAudio(testTone()));
    const Audio before = session.document().audio();
    ScriptEngine engine(session);
    std::string error;
    CHECK(!engine.runString("error('boom')", &error));
    CHECK(!error.empty());
    CHECK(session.document().audio().equals(before));
    error.clear();
    CHECK(!engine.runString("vwave.no_such_function()", &error));
    CHECK(!error.empty());
    CHECK(session.document().audio().equals(before));
}

VWTEST("lua can map samples through a callback") {
    Session session(Document::fromAudio(testTone()));
    ScriptEngine engine(session);
    std::string error;
    const char* script = R"LUA(
local count = 0
vwave.map(0.0, 0.01, function(value, index, channel)
    count = count + 1
    return value * 0.5
end)
assert(count == 480)
vwave.log('mapped ' .. count .. ' samples')
)LUA";
    REQUIRE(engine.runString(script, &error));
    CHECK(error.empty());
    CHECK_NEAR(session.document().audio().sample(0, 100),
               0.25 * std::sin(2.0 * 3.14159265358979323846 * 440.0 *
                               100.0 / 48000.0),
               1e-5);
}

VWTEST("lua can analyse and query sample data") {
    Session session(Document::fromAudio(testTone()));
    ScriptEngine engine(session);
    std::string error;
    const char* script = R"LUA(
local info = vwave.info()
assert(info.duration > 0.09)
local pitch = vwave.detect_pitch()
assert(pitch.frequency > 430 and pitch.frequency < 450)
local value = vwave.sample(0, 0)
assert(type(value) == "number")
local beats = vwave.analyze_beats()
assert(type(beats.bpm) == "number")
)LUA";
    REQUIRE(engine.runString(script, &error));
    CHECK(error.empty());
}

VWTEST("lua can save audio") {
    Session session(Document::fromAudio(testTone()));
    ScriptEngine engine(session);
    TempDirectory directory;
    const std::string path = (directory.path() / "out.wav").string();
    std::string error;
    const std::string script = "vwave.save('" + path + "')";
    REQUIRE(engine.runString(script, &error));
    CHECK(std::filesystem::exists(path));
}

VWTEST("lua script files run with diagnostics") {
    Session session(Document::fromAudio(testTone()));
    ScriptEngine engine(session);
    TempDirectory directory;
    const std::string path = (directory.path() / "script.lua").string();
    {
        std::ofstream out(path);
        out << "vwave.select(0.0, 0.001)\n";
        out << "vwave.gain(-3.0)\n";
    }
    std::string error;
    REQUIRE(engine.runFile(path, &error));
    CHECK(error.empty());
    CHECK_EQ(session.document().undoCount(), size_t{1});
    error.clear();
    CHECK(!engine.runFile("/no/such/script.lua", &error));
    CHECK(!error.empty());
}
