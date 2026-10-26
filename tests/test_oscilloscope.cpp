#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/Oscilloscope.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace vwave;

namespace {

double channelRms(const Audio& audio, int channel) {
    if (audio.numFrames() <= 0) return 0.0;
    double sum = 0.0;
    for (int64_t i = 0; i < audio.numFrames(); ++i) {
        const double value = audio.sample(channel, i);
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(audio.numFrames()));
}

double correlation(const Audio& audio) {
    double lr = 0.0;
    double ll = 0.0;
    double rr = 0.0;
    for (int64_t i = 0; i < audio.numFrames(); ++i) {
        const double left = audio.sample(0, i);
        const double right = audio.sample(1, i);
        lr += left * right;
        ll += left * left;
        rr += right * right;
    }
    const double denominator = std::sqrt(ll * rr);
    return denominator > 1e-12 ? lr / denominator : 0.0;
}

} // namespace

VWTEST("scope shapes generate valid paths") {
    const ScopePath circle = makeScopeCircle(0.8, 128);
    CHECK(circle.valid());
    CHECK(circle.closed);
    CHECK_EQ(circle.points.size(), size_t{128});
    CHECK_NEAR(circle.length(), 2.0 * 3.14159265358979323846 * 0.8, 0.02);

    const ScopePath ellipse = makeScopeEllipse(0.8, 0.4, 64);
    CHECK(ellipse.valid());
    CHECK_EQ(ellipse.points.size(), size_t{64});
    const ScopePath lissajous = makeScopeLissajous(3.0, 2.0, 0.0, 0.8, 256);
    CHECK(lissajous.valid());
    CHECK_EQ(lissajous.points.size(), size_t{256});
    const ScopePath spiral = makeScopeSpiral(0.8, 128, 4);
    CHECK(spiral.valid());
    CHECK(!spiral.closed);
    const ScopePath eight = makeScopeFigureEight(0.8, 128);
    CHECK(eight.valid());
    const ScopePath line = makeScopeLine(0.7);
    CHECK(line.valid());
    CHECK(!line.closed);
    CHECK_EQ(line.points.size(), size_t{2});
    CHECK(makeScopeShape(ScopeShape::Circle, 32).valid());
    CHECK(!makeScopeShape(ScopeShape::Polyline).valid());
}

VWTEST("scope path sampling follows arc length") {
    const ScopePath circle = makeScopeCircle(1.0, 256);
    const ScopePoint first = sampleScopePath(circle, 0.0);
    CHECK_NEAR(first.x, 1.0, 0.01);
    CHECK_NEAR(first.y, 0.0, 0.01);
    const ScopePoint quarter = sampleScopePath(circle, 0.25);
    CHECK_NEAR(quarter.x, 0.0, 0.01);
    CHECK_NEAR(quarter.y, 1.0, 0.01);
    const ScopePoint half = sampleScopePath(circle, 0.5);
    CHECK_NEAR(half.x, -1.0, 0.01);
    CHECK_NEAR(half.y, 0.0, 0.01);
}

VWTEST("scope render produces stereo circles") {
    const ScopePath circle = makeScopeCircle(0.8, 512);
    ScopeRenderOptions options;
    options.sampleRate = 48000;
    options.duration = 0.1;
    options.scanHz = 100.0;
    options.gain = 1.0;
    options.bandLimited = false;
    options.normalize = false;
    const Audio output = renderScopePath(circle, options);
    REQUIRE(!output.empty());
    CHECK_EQ(output.numChannels(), 2);
    CHECK_EQ(output.numFrames(), int64_t{4800});
    CHECK_NEAR(output.sample(0, 0), 0.8, 0.02);
    CHECK_NEAR(output.sample(1, 0), 0.0, 0.02);
    CHECK_NEAR(std::abs(correlation(output)), 0.0, 0.1);
    CHECK(channelRms(output, 0) > 0.3);
    CHECK(channelRms(output, 1) > 0.3);
    for (int64_t i = 0; i < output.numFrames(); ++i) {
        CHECK(std::isfinite(output.sample(0, i)));
        CHECK(std::isfinite(output.sample(1, i)));
    }
}

VWTEST("scope render supports direction normalization and band limiting") {
    const ScopePath circle = makeScopeCircle(0.8, 256);
    ScopeRenderOptions reverse;
    reverse.sampleRate = 48000;
    reverse.duration = 0.02;
    reverse.scanHz = 100.0;
    reverse.gain = 1.0;
    reverse.bandLimited = false;
    reverse.direction = ScopeDirection::Reverse;
    const Audio reversed = renderScopePath(circle, reverse);
    REQUIRE(!reversed.empty());
    CHECK(reversed.sample(1, 120) < -0.7f);

    ScopeRenderOptions limited;
    limited.sampleRate = 48000;
    limited.duration = 0.05;
    limited.scanHz = 400.0;
    limited.bandLimited = true;
    limited.oversampling = 4;
    limited.bandLimitHz = 12000.0;
    limited.normalize = true;
    limited.peak = 0.75;
    const Audio filtered = renderScopePath(circle, limited);
    REQUIRE(!filtered.empty());
    CHECK(filtered.peak() <= 0.751f);
    CHECK(channelRms(filtered, 0) > 0.05);
}

VWTEST("scope path save and load round-trip") {
    ScopePath path = makeScopeLissajous(3.0, 2.0, 0.25, 0.7, 64, "test");
    TempDirectory directory;
    const std::string file = (directory.path() / "path.vscope").string();
    std::string error;
    REQUIRE(saveScopePath(path, file, &error));
    CHECK(error.empty());
    ScopePath loaded;
    REQUIRE(loadScopePath(file, loaded, &error));
    CHECK_EQ(loaded.name, std::string("test"));
    CHECK_EQ(loaded.closed, path.closed);
    CHECK_EQ(loaded.points.size(), path.points.size());
    CHECK_NEAR(loaded.points[10].x, path.points[10].x, 1e-9);
    CHECK_NEAR(loaded.points[10].y, path.points[10].y, 1e-9);
}

VWTEST("scope rejects invalid paths and options") {
    ScopePath empty;
    std::string error;
    CHECK(renderScopePath(empty, {}, &error).empty());
    CHECK(!error.empty());
    const ScopePath line = makeScopeLine();
    ScopeRenderOptions options;
    options.sampleRate = 0;
    CHECK(renderScopePath(line, options, &error).empty());
    options.sampleRate = 48000;
    options.duration = 0.0;
    CHECK(renderScopePath(line, options, &error).empty());
    options.duration = 0.1;
    options.scanHz = 0.0;
    CHECK(renderScopePath(line, options, &error).empty());
}

VWTEST("scope shape and direction names round-trip") {
    for (ScopeShape shape :
         {ScopeShape::Polyline, ScopeShape::Line, ScopeShape::Circle,
          ScopeShape::Ellipse, ScopeShape::Lissajous, ScopeShape::Spiral,
          ScopeShape::FigureEight}) {
        ScopeShape parsed = ScopeShape::Circle;
        CHECK(scopeShapeFromName(scopeShapeName(shape), parsed));
        CHECK(parsed == shape);
    }
    for (ScopeDirection direction :
         {ScopeDirection::Forward, ScopeDirection::Reverse,
          ScopeDirection::PingPong}) {
        ScopeDirection parsed = ScopeDirection::Forward;
        CHECK(scopeDirectionFromName(scopeDirectionName(direction), parsed));
        CHECK(parsed == direction);
    }
    ScopeShape invalid = ScopeShape::Circle;
    CHECK(!scopeShapeFromName("not-a-shape", invalid));
}
