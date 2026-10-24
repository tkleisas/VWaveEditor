#include "test_framework.h"

#include "vwave/vwave_c.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <vector>

VWTEST("c abi creates a session and reads rendered channels") {
    std::vector<float> samples(4800 * 2);
    for (size_t i = 0; i < 4800; ++i) {
        samples[i * 2] = static_cast<float>(i) / 4800.0f;
        samples[i * 2 + 1] = -samples[i * 2];
    }
    VWSession* session =
        vw_session_create_from_interleaved(samples.data(), 2, 4800, 48000);
    REQUIRE(session != nullptr);
    VWDocumentInfo info{};
    REQUIRE(vw_session_info(session, &info) == 0);
    CHECK_EQ(info.abi_version, VWAVE_C_ABI_VERSION);
    CHECK_EQ(info.sample_rate, 48000);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.frames, int64_t{4800});
    CHECK_EQ(info.undo_count, 0);
    const float* channel = vw_session_channel(session, 0);
    REQUIRE(channel != nullptr);
    CHECK_NEAR(channel[100], samples[200], 1e-9);
    CHECK_NEAR(vw_session_channel(session, 1)[100], samples[201], 1e-9);

    std::vector<float> interleaved(4800 * 2);
    CHECK_EQ(vw_session_rendered_interleaved(session, interleaved.data(), 4800),
             int64_t{4800});
    CHECK_NEAR(interleaved[200], samples[200], 1e-9);
    vw_session_destroy(session);
}

VWTEST("c abi selection gain undo and redo") {
    std::vector<float> samples(4800, 1.0f);
    VWSession* session =
        vw_session_create_from_interleaved(samples.data(), 1, 4800, 48000);
    REQUIRE(session != nullptr);
    char error[512] = {};
    REQUIRE(vw_session_select(session, 1000, 2000) == 0);
    REQUIRE(vw_session_gain_db(session, -6.0, error, sizeof(error)) == 0);
    CHECK(error[0] == '\0');
    const float* channel = vw_session_channel(session, 0);
    REQUIRE(channel != nullptr);
    CHECK_NEAR(channel[100], 1.0, 1e-9);
    CHECK_NEAR(channel[1500], 0.501187, 1e-5);
    VWDocumentInfo info{};
    REQUIRE(vw_session_info(session, &info) == 0);
    CHECK_EQ(info.undo_count, 1);
    CHECK_EQ(info.selection_active, 1);

    REQUIRE(vw_session_undo(session) == 0);
    CHECK_NEAR(vw_session_channel(session, 0)[1500], 1.0, 1e-9);
    REQUIRE(vw_session_redo(session) == 0);
    CHECK_NEAR(vw_session_channel(session, 0)[1500], 0.501187, 1e-5);
    vw_session_destroy(session);
}

VWTEST("c abi command dispatch returns json") {
    std::vector<float> samples(1000, 0.25f);
    VWSession* session =
        vw_session_create_from_interleaved(samples.data(), 1, 1000, 48000);
    REQUIRE(session != nullptr);
    char result[2048] = {};
    char error[512] = {};
    REQUIRE(vw_session_execute(session, "info", "{}", result, sizeof(result),
                               error, sizeof(error)) == 0);
    CHECK(error[0] == '\0');
    const nlohmann::json info = nlohmann::json::parse(result);
    CHECK_EQ(info["channels"], 1);
    CHECK_EQ(info["frames"], 1000);

    std::memset(result, 0, sizeof(result));
    REQUIRE(vw_session_execute(session, "normalize",
                               R"({"peak_db":-3.0})", result, sizeof(result),
                               error, sizeof(error)) == 0);
    CHECK_NEAR(vw_session_channel(session, 0)[0], 0.707946, 1e-5);

    std::memset(error, 0, sizeof(error));
    CHECK(vw_session_execute(session, "no_such_command", "{}", result,
                             sizeof(result), error, sizeof(error)) != 0);
    CHECK(error[0] != '\0');
    vw_session_destroy(session);
}

VWTEST("c abi replace and file io") {
    std::vector<float> samples(100, 0.5f);
    VWSession* session =
        vw_session_create_from_interleaved(samples.data(), 1, 100, 48000);
    REQUIRE(session != nullptr);
    std::vector<float> replacement(200, -0.25f);
    char error[512] = {};
    REQUIRE(vw_session_replace_audio(session, replacement.data(), 1, 200,
                                     44100, error, sizeof(error)) == 0);
    VWDocumentInfo info{};
    REQUIRE(vw_session_info(session, &info) == 0);
    CHECK_EQ(info.frames, int64_t{200});
    CHECK_EQ(info.sample_rate, 44100);
    CHECK_NEAR(vw_session_channel(session, 0)[0], -0.25, 1e-9);
    vw_session_destroy(session);
}


VWTEST("c abi json dispatch exposes track commands") {
    std::vector<float> samples(480, 0.25f);
    VWSession* session =
        vw_session_create_from_interleaved(samples.data(), 1, 480, 48000);
    REQUIRE(session != nullptr);
    char result[4096] = {};
    char error[512] = {};
    REQUIRE(vw_session_execute(session, "add_empty_track",
                               R"({"name":"Scratch"})", result,
                               sizeof(result), error, sizeof(error)) == 0);
    REQUIRE(vw_session_execute(session, "track_set",
                               R"({"index":1,"gain_db":-3.0,"muted":true})",
                               result, sizeof(result), error, sizeof(error)) ==
            0);
    REQUIRE(vw_session_execute(session, "tracks", "{}", result,
                               sizeof(result), error, sizeof(error)) == 0);
    const nlohmann::json tracks = nlohmann::json::parse(result);
    CHECK_EQ(tracks["tracks"].size(), size_t{2});
    CHECK_EQ(tracks["tracks"][1]["name"], std::string("Scratch"));
    CHECK_NEAR(tracks["tracks"][1]["gain_db"].get<double>(), -3.0, 1e-9);
    CHECK(tracks["tracks"][1]["muted"].get<bool>());
    vw_session_destroy(session);
}

VWTEST("c abi errors are reported without crashing") {
    CHECK(vw_session_create_from_interleaved(nullptr, 1, 100, 48000) == nullptr);
    VWSession* session = vw_session_create();
    REQUIRE(session != nullptr);
    char error[256] = {};
    CHECK(vw_session_load_audio(session, "/no/such/file.wav", error,
                                sizeof(error)) != 0);
    CHECK(error[0] != '\0');
    CHECK(vw_session_info(session, nullptr) != 0);
    CHECK(vw_session_channel(session, 4) == nullptr);
    vw_session_destroy(session);
    vw_session_destroy(nullptr);
}

VWTEST("c abi time pitch spectral and resample helpers") {
    std::vector<float> samples(4800, 0.0f);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float>(
            0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 48000.0));
    VWSession* session =
        vw_session_create_from_interleaved(samples.data(), 1, 4800, 48000);
    REQUIRE(session != nullptr);
    char error[512] = {};
    REQUIRE(vw_session_select(session, 1000, 2000) == 0);
    REQUIRE(vw_session_time_stretch(session, 2.0, "wsola", error,
                                    sizeof(error)) == 0);
    VWDocumentInfo info{};
    REQUIRE(vw_session_info(session, &info) == 0);
    CHECK_EQ(info.frames, int64_t{5800});
    REQUIRE(vw_session_undo(session) == 0);
    REQUIRE(vw_session_spectral_gain(session, 300.0, 600.0, -12.0, error,
                                     sizeof(error)) == 0);
    REQUIRE(vw_session_undo(session) == 0);
    REQUIRE(vw_session_pitch_shift(session, 12.0, "phase_vocoder", error,
                                   sizeof(error)) == 0);
    REQUIRE(vw_session_undo(session) == 0);
    REQUIRE(vw_session_resample(session, 24000, error, sizeof(error)) == 0);
    REQUIRE(vw_session_info(session, &info) == 0);
    CHECK_EQ(info.sample_rate, 24000);
    CHECK_EQ(info.frames, int64_t{2400});
    vw_session_destroy(session);
}
