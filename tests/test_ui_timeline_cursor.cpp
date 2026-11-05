#include "test_framework.h"

#include "ui/editor/TimelineCursor.h"

using namespace vwave::ui;

namespace {

TimelineCursorGeometry geometry() {
    TimelineCursorGeometry geometry;
    geometry.waveArea = {0, 50, 500, 300};
    geometry.ruler = {0, 30, 500, 20};
    geometry.overview = {0, 10, 500, 20};
    geometry.tracks.push_back(
        {{0, 50, 100, 150}, {100, 50, 400, 150}, 0, 220, 420, true});
    geometry.selection = {250, 50, 100, 150};
    geometry.selectionActive = true;
    geometry.clipToolEnabled = true;
    geometry.clips.push_back({{150, 55, 120, 40}, 0, 0});
    return geometry;
}

} // namespace

VWTEST("timeline cursor distinguishes ruler overview and headers") {
    const auto hit = geometry();
    CHECK(timelineCursorAt(hit, {10, 15}) == CursorShape::Hand);
    CHECK(timelineCursorAt(hit, {10, 35}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {10, 60}) == CursorShape::SizeAll);
    CHECK(timelineCursorAt(hit, {600, 600}) == CursorShape::Arrow);
}

VWTEST("timeline cursor detects clip body and edges") {
    const auto hit = geometry();
    CHECK(timelineCursorAt(hit, {200, 70}) == CursorShape::SizeAll);
    CHECK(timelineCursorAt(hit, {152, 70}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {268, 70}) == CursorShape::SizeWE);
}

VWTEST("timeline cursor detects selection handles") {
    auto hit = geometry();
    hit.clipToolEnabled = false;
    hit.clips.clear();
    CHECK(timelineCursorAt(hit, {251, 100}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {349, 100}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {345, 195}) == CursorShape::SizeAll);
}

VWTEST("timeline cursor detects track trim and stretch handles") {
    auto hit = geometry();
    hit.clipToolEnabled = false;
    hit.clips.clear();
    hit.selectionActive = false;
    CHECK(timelineCursorAt(hit, {218, 55}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {412, 55}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {221, 55}) == CursorShape::SizeWE);
    CHECK(timelineCursorAt(hit, {419, 190}) == CursorShape::SizeWE);
}

VWTEST("timeline cursor uses crosshair for empty lanes") {
    auto hit = geometry();
    hit.clipToolEnabled = false;
    hit.clips.clear();
    hit.selectionActive = false;
    CHECK(timelineCursorAt(hit, {300, 100}) == CursorShape::Crosshair);
}
