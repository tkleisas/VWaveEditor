#include "vwave/Scripting.h"

#include "vwave/Document.h"
#include "vwave/Session.h"
#include "vwave/Version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

#ifdef VWAVE_HAS_LUA

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace vwave {

struct ScriptEngine::Impl {
    explicit Impl(Session& sessionValue) : session(&sessionValue) {}
    ~Impl() {
        if (state) lua_close(state);
    }
    Session* session = nullptr;
    lua_State* state = nullptr;
};

namespace {

Session* sessionFrom(lua_State* L) {
    return static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
}

void pushJson(lua_State* L, const nlohmann::json& value);

void pushJsonObject(lua_State* L, const nlohmann::json& value) {
    lua_newtable(L);
    for (auto it = value.begin(); it != value.end(); ++it) {
        lua_pushstring(L, it.key().c_str());
        pushJson(L, it.value());
        lua_settable(L, -3);
    }
}

void pushJson(lua_State* L, const nlohmann::json& value) {
    if (value.is_null()) lua_pushnil(L);
    else if (value.is_boolean()) lua_pushboolean(L, value.get<bool>());
    else if (value.is_number_integer()) lua_pushinteger(L, value.get<lua_Integer>());
    else if (value.is_number_unsigned())
        lua_pushinteger(L, static_cast<lua_Integer>(value.get<uint64_t>()));
    else if (value.is_number()) lua_pushnumber(L, value.get<lua_Number>());
    else if (value.is_string())
        lua_pushstring(L, value.get<std::string>().c_str());
    else if (value.is_array()) {
        lua_newtable(L);
        int index = 1;
        for (const auto& item : value) {
            pushJson(L, item);
            lua_rawseti(L, -2, index++);
        }
    } else {
        pushJsonObject(L, value);
    }
}

nlohmann::json luaToJson(lua_State* L, int index, int depth = 0) {
    if (depth > 32) return nullptr;
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        return nullptr;
    case LUA_TBOOLEAN:
        return static_cast<bool>(lua_toboolean(L, index));
    case LUA_TNUMBER: {
        const lua_Number number = lua_tonumber(L, index);
        if (std::floor(number) == number &&
            number >= static_cast<lua_Number>(std::numeric_limits<int64_t>::min()) &&
            number <= static_cast<lua_Number>(std::numeric_limits<int64_t>::max()))
            return static_cast<int64_t>(number);
        return static_cast<double>(number);
    }
    case LUA_TSTRING:
        return std::string(lua_tostring(L, index));
    case LUA_TTABLE: {
        const size_t length = lua_rawlen(L, index);
        bool isArray = length > 0;
        if (isArray) {
            for (size_t i = 1; i <= length; ++i) {
                lua_rawgeti(L, index, static_cast<lua_Integer>(i));
                const bool nil = lua_isnil(L, -1);
                lua_pop(L, 1);
                if (nil) {
                    isArray = false;
                    break;
                }
            }
        }
        if (isArray) {
            nlohmann::json array = nlohmann::json::array();
            for (size_t i = 1; i <= length; ++i) {
                lua_rawgeti(L, index, static_cast<lua_Integer>(i));
                array.push_back(luaToJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return array;
        }
        nlohmann::json object = nlohmann::json::object();
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const char* key = lua_tostring(L, -2);
                object[key] = luaToJson(L, -1, depth + 1);
            }
            lua_pop(L, 1);
        }
        return object;
    }
    default:
        return nullptr;
    }
}

int pushCommandResult(lua_State* L, const nlohmann::json& result,
                      const std::string& error) {
    if (result.is_null()) {
        lua_pushnil(L);
        lua_pushstring(L, error.empty() ? "command failed" : error.c_str());
        return 2;
    }
    pushJson(L, result);
    return 1;
}

int runCommand(lua_State* L, const std::string& command,
               const nlohmann::json& params) {
    Session* session = sessionFrom(L);
    if (!session) {
        lua_pushnil(L);
        lua_pushstring(L, "script engine has no session");
        return 2;
    }
    std::string error;
    const nlohmann::json result = session->execute(command, params, &error);
    return pushCommandResult(L, result, error);
}

nlohmann::json rangeParams(lua_State* L, int firstOptional) {
    nlohmann::json params = nlohmann::json::object();
    if (lua_gettop(L) >= firstOptional + 1 &&
        lua_isnumber(L, firstOptional) && lua_isnumber(L, firstOptional + 1)) {
        params["start"] = lua_tonumber(L, firstOptional);
        params["end"] = lua_tonumber(L, firstOptional + 1);
    }
    return params;
}

int luaLoad(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    return runCommand(L, "load_audio", {{"path", path}});
}

int luaSave(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    nlohmann::json params = {{"path", path}};
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        params["format"] = luaL_checkstring(L, 2);
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
        params["bit_depth"] = luaL_checkstring(L, 3);
    return runCommand(L, "save_audio", params);
}

int luaInfo(lua_State* L) { return runCommand(L, "info", nlohmann::json::object()); }

int luaSelect(lua_State* L) {
    const double start = luaL_checknumber(L, 1);
    const double end = luaL_checknumber(L, 2);
    return runCommand(L, "select", {{"start", start}, {"end", end}});
}

int luaSelectAll(lua_State* L) {
    return runCommand(L, "select_all", nlohmann::json::object());
}

int luaClearSelection(lua_State* L) {
    return runCommand(L, "clear_selection", nlohmann::json::object());
}

int luaUndo(lua_State* L) {
    return runCommand(L, "undo", nlohmann::json::object());
}

int luaRedo(lua_State* L) {
    return runCommand(L, "redo", nlohmann::json::object());
}

int luaApply(lua_State* L) {
    const char* command = luaL_checkstring(L, 1);
    nlohmann::json params = nlohmann::json::object();
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        params = luaToJson(L, 2);
        if (!params.is_object()) {
            lua_pushnil(L);
            lua_pushstring(L, "parameters must be a table");
            return 2;
        }
    }
    return runCommand(L, command, params);
}

int luaGain(lua_State* L) {
    const double db = luaL_checknumber(L, 1);
    nlohmann::json params = rangeParams(L, 2);
    params["db"] = db;
    return runCommand(L, "gain", params);
}

int luaNormalize(lua_State* L) {
    const double peak = luaL_optnumber(L, 1, -1.0);
    return runCommand(L, "normalize", {{"peak_db", peak}});
}

int luaFadeIn(lua_State* L) {
    return runCommand(L, "fade_in", rangeParams(L, 1));
}

int luaFadeOut(lua_State* L) {
    return runCommand(L, "fade_out", rangeParams(L, 1));
}

int luaReverse(lua_State* L) {
    return runCommand(L, "reverse", rangeParams(L, 1));
}

int luaInvert(lua_State* L) {
    return runCommand(L, "invert", rangeParams(L, 1));
}

int luaSilence(lua_State* L) {
    return runCommand(L, "silence", rangeParams(L, 1));
}

int luaRemoveDc(lua_State* L) {
    return runCommand(L, "dc_remove", rangeParams(L, 1));
}

int luaCut(lua_State* L) {
    return runCommand(L, "cut", rangeParams(L, 1));
}

int luaCopy(lua_State* L) {
    return runCommand(L, "copy", rangeParams(L, 1));
}

int luaDelete(lua_State* L) {
    return runCommand(L, "delete", rangeParams(L, 1));
}

int luaPaste(lua_State* L) {
    nlohmann::json params = nlohmann::json::object();
    if (lua_gettop(L) >= 1 && lua_isnumber(L, 1))
        params["at"] = lua_tonumber(L, 1);
    return runCommand(L, "paste", params);
}

int luaCrop(lua_State* L) {
    return runCommand(L, "crop", rangeParams(L, 1));
}

int luaStretch(lua_State* L) {
    const double ratio = luaL_checknumber(L, 1);
    const char* algorithm = luaL_optstring(L, 2, "wsola");
    nlohmann::json params = rangeParams(L, 3);
    params["ratio"] = ratio;
    params["algorithm"] = algorithm;
    return runCommand(L, "time_stretch", params);
}

int luaPaulStretch(lua_State* L) {
    const double ratio = luaL_checknumber(L, 1);
    nlohmann::json params = rangeParams(L, 2);
    params["ratio"] = ratio;
    return runCommand(L, "paulstretch", params);
}

int luaPitch(lua_State* L) {
    const double semitones = luaL_checknumber(L, 1);
    const char* algorithm = luaL_optstring(L, 2, "phase_vocoder");
    nlohmann::json params = rangeParams(L, 3);
    params["semitones"] = semitones;
    params["algorithm"] = algorithm;
    return runCommand(L, "pitch_shift", params);
}

int luaResample(lua_State* L) {
    const int sampleRate = static_cast<int>(luaL_checkinteger(L, 1));
    return runCommand(L, "resample", {{"sample_rate", sampleRate}});
}

int luaSpectralGain(lua_State* L) {
    const double low = luaL_checknumber(L, 1);
    const double high = luaL_checknumber(L, 2);
    const double db = luaL_checknumber(L, 3);
    nlohmann::json params = rangeParams(L, 4);
    params["low_hz"] = low;
    params["high_hz"] = high;
    params["db"] = db;
    return runCommand(L, "spectral_gain", params);
}

int luaSpectralFilter(lua_State* L) {
    const char* type = luaL_checkstring(L, 1);
    const double cutoff = luaL_checknumber(L, 2);
    return runCommand(L, "spectral_filter", {{"type", type},
                                             {"cutoff_hz", cutoff}});
}

int luaSpectralGate(lua_State* L) {
    const double threshold = luaL_checknumber(L, 1);
    return runCommand(L, "spectral_gate", {{"threshold_db", threshold}});
}

int luaAnalyzeBeats(lua_State* L) {
    const double sensitivity = luaL_optnumber(L, 1, 0.5);
    return runCommand(L, "analyze_beats", {{"sensitivity", sensitivity}});
}

int luaDetectPitch(lua_State* L) {
    nlohmann::json params = rangeParams(L, 1);
    return runCommand(L, "detect_pitch", params);
}

int luaSlices(lua_State* L) {
    const char* mode = luaL_optstring(L, 1, "transients");
    nlohmann::json params = {{"mode", mode}};
    if (lua_gettop(L) >= 2 && lua_isnumber(L, 2))
        params["sensitivity"] = lua_tonumber(L, 2);
    return runCommand(L, "slices", params);
}

int luaExportSlices(lua_State* L) {
    const char* directory = luaL_checkstring(L, 1);
    nlohmann::json params = {{"directory", directory}};
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        params["prefix"] = luaL_checkstring(L, 2);
    return runCommand(L, "export_slices", params);
}

int luaWavetable(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    nlohmann::json params = {{"path", path}};
    if (lua_gettop(L) >= 2 && lua_isnumber(L, 2))
        params["frame_size"] = static_cast<int>(lua_tointeger(L, 2));
    if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
        params["frame_count"] = static_cast<int>(lua_tointeger(L, 3));
    return runCommand(L, "wavetable", params);
}

int luaSynth(lua_State* L) {
    const char* wavetable = luaL_checkstring(L, 1);
    const double note = luaL_optnumber(L, 2, 60.0);
    const double duration = luaL_optnumber(L, 3, 1.0);
    const char* out = luaL_optstring(L, 4, nullptr);
    nlohmann::json params = {{"wavetable", wavetable},
                             {"midi_note", note},
                             {"duration", duration}};
    if (out) params["out"] = out;
    return runCommand(L, "synth_note", params);
}

int luaSweep(lua_State* L) {
    const char* out = luaL_checkstring(L, 1);
    const double seconds = luaL_optnumber(L, 2, 5.0);
    return runCommand(L, "generate_sweep",
                      {{"out", out}, {"seconds", seconds}});
}

int luaDeconvolve(lua_State* L) {
    const char* recorded = luaL_checkstring(L, 1);
    const char* sweep = luaL_checkstring(L, 2);
    const char* out = luaL_checkstring(L, 3);
    return runCommand(L, "deconvolve",
                      {{"recorded", recorded}, {"sweep", sweep}, {"out", out}});
}


int luaAddTrack(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    nlohmann::json params = {{"path", path}};
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        params["name"] = luaL_checkstring(L, 2);
    if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
        params["start"] = lua_tonumber(L, 3);
    return runCommand(L, "add_track", params);
}

int luaAddEmptyTrack(lua_State* L) {
    nlohmann::json params = nlohmann::json::object();
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1))
        params["name"] = luaL_checkstring(L, 1);
    if (lua_gettop(L) >= 2 && lua_isnumber(L, 2))
        params["start"] = lua_tonumber(L, 2);
    return runCommand(L, "add_empty_track", params);
}

int luaSelectTrack(lua_State* L) {
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    return runCommand(L, "select_track", {{"index", index}});
}

int luaRemoveTrack(lua_State* L) {
    nlohmann::json params = nlohmann::json::object();
    if (lua_gettop(L) >= 1 && lua_isnumber(L, 1))
        params["index"] = static_cast<int>(lua_tointeger(L, 1));
    return runCommand(L, "remove_track", params);
}

int luaTracks(lua_State* L) {
    return runCommand(L, "tracks", nlohmann::json::object());
}

int luaTrackSet(lua_State* L) {
    if (lua_gettop(L) < 2) {
        lua_pushnil(L);
        lua_pushstring(L, "track_set requires index and parameter table");
        return 2;
    }
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);
    nlohmann::json params = luaToJson(L, 2);
    if (!params.is_object()) {
        lua_pushnil(L);
        lua_pushstring(L, "track parameters must be a table");
        return 2;
    }
    params["index"] = index;
    return runCommand(L, "track_set", params);
}

int luaSetTrackGain(lua_State* L) {
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    const double db = luaL_checknumber(L, 2);
    return runCommand(L, "track_set", {{"index", index}, {"gain_db", db}});
}

int luaSetTrackPan(lua_State* L) {
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    const double pan = luaL_checknumber(L, 2);
    return runCommand(L, "track_set", {{"index", index}, {"pan", pan}});
}

int luaSetTrackArm(lua_State* L) {
    const int index = static_cast<int>(luaL_checkinteger(L, 1));
    const bool armed = lua_toboolean(L, 2) != 0;
    return runCommand(L, "track_set", {{"index", index}, {"armed", armed}});
}

int luaSaveMixdown(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    nlohmann::json params = {{"path", path}};
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        params["format"] = luaL_checkstring(L, 2);
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
        params["bit_depth"] = luaL_checkstring(L, 3);
    return runCommand(L, "save_mixdown", params);
}

int luaSample(lua_State* L) {
    const int channel = static_cast<int>(luaL_checkinteger(L, 1));
    const int64_t frame = static_cast<int64_t>(luaL_checkinteger(L, 2));
    Session* session = sessionFrom(L);
    if (!session) {
        lua_pushnil(L);
        lua_pushstring(L, "script engine has no session");
        return 2;
    }
    const Audio& audio = session->document().audio();
    if (channel < 0 || channel >= audio.numChannels() || frame < 0 ||
        frame >= audio.numFrames()) {
        lua_pushnil(L);
        lua_pushstring(L, "sample out of range");
        return 2;
    }
    lua_pushnumber(L, audio.sample(channel, frame));
    return 1;
}

int luaMap(lua_State* L) {
    const double startSeconds = luaL_checknumber(L, 1);
    const double endSeconds = luaL_checknumber(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    Session* session = sessionFrom(L);
    if (!session) {
        lua_pushnil(L);
        lua_pushstring(L, "script engine has no session");
        return 2;
    }
    Audio working = session->document().audio();
    if (working.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "document is empty");
        return 2;
    }
    const double sampleRate = working.sampleRate();
    if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) ||
        startSeconds < 0.0 || endSeconds <= startSeconds) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid sample map range");
        return 2;
    }
    const int64_t start = std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(startSeconds * sampleRate)), 0,
        working.numFrames());
    const int64_t end = std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(endSeconds * sampleRate)), start,
        working.numFrames());
    if (end <= start) {
        lua_pushnil(L);
        lua_pushstring(L, "sample map range is empty");
        return 2;
    }
    const int functionIndex = lua_absindex(L, 3);
    int64_t changed = 0;
    for (int64_t frame = start; frame < end; ++frame) {
        for (int channel = 0; channel < working.numChannels(); ++channel) {
            lua_pushvalue(L, functionIndex);
            lua_pushnumber(L, working.sample(channel, frame));
            lua_pushinteger(L, static_cast<lua_Integer>(frame));
            lua_pushinteger(L, static_cast<lua_Integer>(channel));
            if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
                const char* message = lua_tostring(L, -1);
                lua_pushnil(L);
                lua_pushstring(L, message ? message : "sample callback failed");
                lua_remove(L, -3);
                return 2;
            }
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                lua_pushstring(L, "sample callback must return a number");
                return 2;
            }
            working.sample(channel, frame) = static_cast<float>(
                lua_tonumber(L, -1));
            lua_pop(L, 1);
            ++changed;
        }
    }
    std::string error;
    if (!session->document().apply(
            Operation::replaceAll(std::move(working)), &error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.empty() ? "cannot store mapped samples"
                                        : error.c_str());
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(changed));
    return 1;
}

int luaLog(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    std::fprintf(stderr, "[vwave] %s\n", message);
    return 0;
}

void registerFunction(lua_State* L, Session* session, const char* name,
                      lua_CFunction fn) {
    lua_pushlightuserdata(L, session);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

} // namespace

ScriptEngine::ScriptEngine(Session& session)
    : m_impl(std::make_unique<Impl>(session)) {
    m_impl->state = luaL_newstate();
    if (!m_impl->state) return;
    luaL_openlibs(m_impl->state);
    lua_State* L = m_impl->state;
    lua_newtable(L);
    registerFunction(L, &session, "load", luaLoad);
    registerFunction(L, &session, "save", luaSave);
    registerFunction(L, &session, "info", luaInfo);
    registerFunction(L, &session, "select", luaSelect);
    registerFunction(L, &session, "select_all", luaSelectAll);
    registerFunction(L, &session, "clear_selection", luaClearSelection);
    registerFunction(L, &session, "undo", luaUndo);
    registerFunction(L, &session, "redo", luaRedo);
    registerFunction(L, &session, "apply", luaApply);
    registerFunction(L, &session, "gain", luaGain);
    registerFunction(L, &session, "normalize", luaNormalize);
    registerFunction(L, &session, "fade_in", luaFadeIn);
    registerFunction(L, &session, "fade_out", luaFadeOut);
    registerFunction(L, &session, "reverse", luaReverse);
    registerFunction(L, &session, "invert", luaInvert);
    registerFunction(L, &session, "silence", luaSilence);
    registerFunction(L, &session, "dc_remove", luaRemoveDc);
    registerFunction(L, &session, "copy", luaCopy);
    registerFunction(L, &session, "cut", luaCut);
    registerFunction(L, &session, "delete", luaDelete);
    registerFunction(L, &session, "paste", luaPaste);
    registerFunction(L, &session, "crop", luaCrop);
    registerFunction(L, &session, "stretch", luaStretch);
    registerFunction(L, &session, "paulstretch", luaPaulStretch);
    registerFunction(L, &session, "pitch", luaPitch);
    registerFunction(L, &session, "resample", luaResample);
    registerFunction(L, &session, "spectral_gain", luaSpectralGain);
    registerFunction(L, &session, "spectral_filter", luaSpectralFilter);
    registerFunction(L, &session, "spectral_gate", luaSpectralGate);
    registerFunction(L, &session, "analyze_beats", luaAnalyzeBeats);
    registerFunction(L, &session, "detect_pitch", luaDetectPitch);
    registerFunction(L, &session, "slices", luaSlices);
    registerFunction(L, &session, "export_slices", luaExportSlices);
    registerFunction(L, &session, "wavetable", luaWavetable);
    registerFunction(L, &session, "synth", luaSynth);
    registerFunction(L, &session, "sweep", luaSweep);
    registerFunction(L, &session, "deconvolve", luaDeconvolve);
    registerFunction(L, &session, "add_track", luaAddTrack);
    registerFunction(L, &session, "add_empty_track", luaAddEmptyTrack);
    registerFunction(L, &session, "select_track", luaSelectTrack);
    registerFunction(L, &session, "remove_track", luaRemoveTrack);
    registerFunction(L, &session, "tracks", luaTracks);
    registerFunction(L, &session, "track_set", luaTrackSet);
    registerFunction(L, &session, "set_track_gain", luaSetTrackGain);
    registerFunction(L, &session, "set_track_pan", luaSetTrackPan);
    registerFunction(L, &session, "set_track_arm", luaSetTrackArm);
    registerFunction(L, &session, "save_mixdown", luaSaveMixdown);
    registerFunction(L, &session, "sample", luaSample);
    registerFunction(L, &session, "map", luaMap);
    registerFunction(L, &session, "log", luaLog);
    lua_pushstring(L, VWAVE_VERSION);
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "vwave");
}

ScriptEngine::~ScriptEngine() = default;

bool ScriptEngine::runString(const std::string& code, std::string* error) {
    if (error) error->clear();
    if (!m_impl || !m_impl->state) {
        if (error) *error = "Lua state is not available";
        return false;
    }
    lua_State* L = m_impl->state;
    const int base = lua_gettop(L);
    if (luaL_loadbuffer(L, code.data(), code.size(), "@<vwave-script>") !=
        LUA_OK) {
        const char* message = lua_tostring(L, -1);
        if (error) *error = message ? message : "Lua load error";
        lua_settop(L, base);
        return false;
    }
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        if (error) *error = message ? message : "Lua runtime error";
        lua_settop(L, base);
        return false;
    }
    lua_settop(L, base);
    return true;
}

bool ScriptEngine::runFile(const std::string& path, std::string* error) {
    if (error) error->clear();
    if (!m_impl || !m_impl->state) {
        if (error) *error = "Lua state is not available";
        return false;
    }
    lua_State* L = m_impl->state;
    const int base = lua_gettop(L);
    if (luaL_loadfile(L, path.c_str()) != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        if (error) *error = message ? message : "cannot load Lua script";
        lua_settop(L, base);
        return false;
    }
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        if (error) *error = message ? message : "Lua runtime error";
        lua_settop(L, base);
        return false;
    }
    lua_settop(L, base);
    return true;
}

lua_State* ScriptEngine::state() const {
    return m_impl ? m_impl->state : nullptr;
}

} // namespace vwave

#else

namespace vwave {

struct ScriptEngine::Impl {};

ScriptEngine::ScriptEngine(Session&) : m_impl(std::make_unique<Impl>()) {}
ScriptEngine::~ScriptEngine() = default;

bool ScriptEngine::runString(const std::string&, std::string* error) {
    if (error) *error = "Lua support is not built";
    return false;
}

bool ScriptEngine::runFile(const std::string&, std::string* error) {
    if (error) *error = "Lua support is not built";
    return false;
}

lua_State* ScriptEngine::state() const { return nullptr; }

} // namespace vwave

#endif
