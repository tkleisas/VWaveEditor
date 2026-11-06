#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/ScopeComposition.h"

#include <cmath>
#include <fstream>

using namespace vwave;

namespace {

constexpr double kPi = 3.14159265358979323846;

ScopePath linePath(ScopePoint a, ScopePoint b, bool closed = false) {
    ScopePath path;
    path.closed = closed;
    path.points = {a, b};
    return path;
}

ScopeDrawing constantDrawing(const std::string& id, ScopePoint point) {
    ScopeDrawing drawing;
    drawing.id = id;
    drawing.name = id;
    drawing.strokes.push_back(linePath(point, point));
    return drawing;
}

ScopeComposition constantComposition() {
    ScopeComposition composition;
    composition.duration = 1.0;
    composition.drawings = {constantDrawing("a", {-0.5, 0.0}),
                            constantDrawing("b", {0.5, 0.0})};
    ScopeKeyframe first;
    first.time = 0.0;
    first.drawing = "a";
    ScopeKeyframe second;
    second.time = 1.0;
    second.drawing = "b";
    composition.keyframes = {first, second};
    return composition;
}

ScopeRenderOptions renderOptions() {
    ScopeRenderOptions options;
    options.sampleRate = 1000;
    options.duration = 1.0;
    options.scanHz = 1.0;
    options.gain = 1.0;
    options.bandLimited = false;
    options.normalize = false;
    options.smoothing = 0.0;
    options.oversampling = 1;
    return options;
}

} // namespace

VWTEST("scope transform math and easing behave predictably") {
    ScopeTransform transform;
    transform.scaleX = 2.0;
    transform.scaleY = 3.0;
    transform.rotation = kPi / 2.0;
    transform.translateX = 0.1;
    const ScopePoint rotated =
        applyScopeTransform(transform, {1.0, 0.0});
    CHECK_NEAR(rotated.x, 0.1, 1e-9);
    CHECK_NEAR(rotated.y, 2.0, 1e-9);

    ScopeTransform start;
    ScopeTransform end;
    end.translateX = 1.0;
    end.scaleX = 2.0;
    end.rotation = 3.0 * kPi / 2.0;
    const ScopeTransform middle =
        interpolateScopeTransform(start, end, 0.5);
    CHECK_NEAR(middle.translateX, 0.5, 1e-9);
    CHECK_NEAR(middle.scaleX, 1.5, 1e-9);
    CHECK_NEAR(middle.rotation, -kPi / 4.0, 1e-9);

    CHECK_NEAR(scopeEasingValue(ScopeEasing::Linear, 0.25), 0.25, 1e-9);
    CHECK_NEAR(scopeEasingValue(ScopeEasing::EaseIn, 0.5), 0.25, 1e-9);
    CHECK_NEAR(scopeEasingValue(ScopeEasing::EaseOut, 0.5), 0.75, 1e-9);
    CHECK_NEAR(scopeEasingValue(ScopeEasing::Smoothstep, 0.5), 0.5, 1e-9);
    CHECK_NEAR(scopeEasingValue(ScopeEasing::Hold, 0.99), 0.0, 1e-9);
}

VWTEST("scope drawings flatten multiple strokes with travel connectors") {
    ScopeDrawing drawing;
    drawing.id = "drawing";
    drawing.name = "drawing";
    drawing.strokes.push_back(linePath({0.0, 0.0}, {1.0, 0.0}));
    drawing.strokes.push_back(linePath({2.0, 0.0}, {3.0, 0.0}));
    const ScopePath flat = drawing.flatten();
    CHECK_EQ(flat.points.size(), size_t{4});
    CHECK_NEAR(flat.points[1].x, 1.0, 1e-9);
    CHECK_NEAR(flat.points[2].x, 2.0, 1e-9);
    CHECK(!flat.closed);
    CHECK(drawing.valid());
}

VWTEST("scope composition renders hold and crossfade paths") {
    ScopeComposition hold = constantComposition();
    hold.duration = 2.0;
    hold.keyframes[1].transition = ScopeTransition::Hold;
    ScopeRenderOptions options = renderOptions();
    options.duration = hold.duration;
    Audio held = renderScopeComposition(hold, options);
    REQUIRE(!held.empty());
    CHECK_NEAR(held.sample(0, 500), -0.5, 1e-6);
    CHECK_NEAR(held.sample(0, 1500), 0.5, 1e-6);

    ScopeComposition crossfade = constantComposition();
    crossfade.keyframes[0].transition = ScopeTransition::Crossfade;
    Audio blended = renderScopeComposition(crossfade, options);
    REQUIRE(!blended.empty());
    CHECK_NEAR(blended.sample(0, 0), -0.5, 1e-6);
    CHECK_NEAR(blended.sample(0, 500), 0.0, 1e-6);
    CHECK_NEAR(blended.sample(0, 999), 0.499, 1e-3);
}

VWTEST("scope composition animates transforms over time") {
    ScopeComposition composition;
    composition.duration = 1.0;
    composition.drawings = {constantDrawing("a", {0.5, 0.0})};
    ScopeKeyframe first;
    first.time = 0.0;
    first.drawing = "a";
    ScopeKeyframe second;
    second.time = 1.0;
    second.drawing = "a";
    second.transform.translateX = 0.1;
    composition.keyframes = {first, second};
    ScopeRenderOptions options = renderOptions();
    const Audio audio = renderScopeComposition(composition, options);
    REQUIRE(!audio.empty());
    CHECK_NEAR(audio.sample(0, 0), 0.5, 1e-6);
    CHECK_NEAR(audio.sample(0, 500), 0.55, 1e-6);
    CHECK_NEAR(audio.sample(0, 999), 0.6, 1e-3);
}

VWTEST("scope composition validates keyframes and drawings") {
    ScopeComposition composition = constantComposition();
    CHECK(composition.validate());
    composition.keyframes[0].time = 2.0;
    std::string error;
    CHECK(!composition.validate(&error));
    CHECK(!error.empty());

    composition = constantComposition();
    composition.keyframes[1].drawing = "missing";
    error.clear();
    CHECK(!composition.validate(&error));
    CHECK(!error.empty());

    composition = constantComposition();
    composition.drawings.clear();
    error.clear();
    CHECK(!composition.validate(&error));
    CHECK(!error.empty());
}

VWTEST("scope composition json round-trips and loads legacy paths") {
    ScopeComposition composition = constantComposition();
    composition.name = "motion";
    composition.drawings[0].strokes.front().name = "line";
    composition.keyframes[1].transform.rotation = kPi / 4.0;
    composition.keyframes[1].easing = ScopeEasing::EaseInOut;
    composition.keyframes[1].transition = ScopeTransition::Crossfade;

    const auto json = scopeCompositionToJson(composition);
    ScopeComposition parsed;
    std::string error;
    REQUIRE(scopeCompositionFromJson(json, parsed, &error));
    CHECK_EQ(parsed.name, composition.name);
    CHECK_EQ(parsed.drawings.size(), size_t{2});
    CHECK_EQ(parsed.keyframes.size(), size_t{2});
    CHECK(parsed.keyframes[1].transition == ScopeTransition::Crossfade);
    CHECK(parsed.keyframes[1].easing == ScopeEasing::EaseInOut);
    CHECK_NEAR(parsed.keyframes[1].transform.rotation, kPi / 4.0, 1e-9);

    TempPath file(".vscope");
    REQUIRE(saveScopeComposition(composition, file.string(), &error));
    ScopeComposition loaded;
    REQUIRE(loadScopeComposition(file.string(), loaded, &error));
    CHECK_EQ(loaded.drawings.size(), size_t{2});

    const nlohmann::json legacy = {
        {"format", "vwave-scope"},
        {"version", 1},
        {"name", "legacy"},
        {"closed", false},
        {"points", {{0.0, 0.0}, {1.0, 1.0}}}};
    ScopeComposition migrated;
    REQUIRE(scopeCompositionFromJson(legacy, migrated, &error));
    CHECK_EQ(migrated.drawings.size(), size_t{1});
    CHECK_EQ(migrated.keyframes.size(), size_t{1});
    CHECK_EQ(migrated.keyframes[0].drawing, std::string("path"));
}
