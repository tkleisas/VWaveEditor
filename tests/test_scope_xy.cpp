#include "test_framework.h"

#include "vwave/ScopeXY.h"

#include <cmath>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

float neighborhood(const ScopeXYPlot& plot, int x, int y, int radius = 2) {
    float value = 0.0f;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            value = std::max(value, plot.at(x + dx, y + dy));
    return value;
}

} // namespace

VWTEST("scope xy plot draws a stereo circle") {
    Audio audio(2, 1024, 48000);
    for (int64_t i = 0; i < audio.numFrames(); ++i) {
        const double angle =
            2.0 * kPi * static_cast<double>(i) /
            static_cast<double>(audio.numFrames());
        audio.sample(0, i) = static_cast<float>(0.8 * std::cos(angle));
        audio.sample(1, i) = static_cast<float>(0.8 * std::sin(angle));
    }
    ScopeXYOptions options;
    options.width = 64;
    options.height = 64;
    const ScopeXYPlot plot = renderScopeXY(audio, options);
    REQUIRE(plot.valid());
    CHECK_EQ(plot.frames, audio.numFrames());
    CHECK(plot.peak > 0.0f);
    const int x = 57;
    const int y = 31;
    CHECK(neighborhood(plot, x, y) > 0.0f);
    CHECK_NEAR(plot.at(32, 32), 0.0f, 1e-6);
}

VWTEST("scope xy plot connects samples and honors ranges") {
    Audio audio(2, 4, 48000);
    audio.sample(0, 0) = -0.5f;
    audio.sample(0, 1) = 0.0f;
    audio.sample(0, 2) = 0.5f;
    audio.sample(0, 3) = 0.5f;
    for (int64_t i = 0; i < 4; ++i) audio.sample(1, i) = 0.0f;
    ScopeXYOptions options;
    options.width = 33;
    options.height = 33;
    options.connect = true;
    const ScopeXYPlot plot = renderScopeXY(audio, options);
    REQUIRE(plot.valid());
    CHECK(neighborhood(plot, 8, 16, 1) > 0.0f);
    CHECK(neighborhood(plot, 16, 16, 1) > 0.0f);
    CHECK(neighborhood(plot, 24, 16, 1) > 0.0f);

    options.range = {1, 3};
    options.stride = 2;
    const ScopeXYPlot ranged = renderScopeXY(audio, options);
    REQUIRE(ranged.valid());
    CHECK_EQ(ranged.frames, int64_t{1});
}

VWTEST("scope xy plot rejects invalid audio") {
    std::string error;
    Audio mono(1, 16, 48000);
    CHECK(!renderScopeXY(mono, {}, &error).valid());
    CHECK(!error.empty());
    error.clear();
    Audio empty(2, 0, 48000);
    CHECK(!renderScopeXY(empty, {}, &error).valid());
    CHECK(!error.empty());
}
