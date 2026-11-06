#include "test_framework.h"
#include "test_helpers.h"

#include "vwave/ScopeSvg.h"

#include <fstream>

using namespace vwave;

namespace {

bool near(double actual, double expected, double tolerance = 1e-6) {
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

VWTEST("scope svg parses rectangles and normalizes coordinates") {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\">"
        "<rect x=\"10\" y=\"20\" width=\"40\" height=\"20\"/>"
        "</svg>";
    ScopeDrawing drawing;
    std::string error;
    REQUIRE(parseScopeSvg(svg, drawing, {}, &error));
    CHECK(error.empty());
    REQUIRE(drawing.strokes.size() == size_t{1});
    const ScopePath& path = drawing.strokes.front();
    CHECK(path.closed);
    CHECK_EQ(path.points.size(), size_t{4});
    double minimumX = 2.0;
    double maximumX = -2.0;
    double minimumY = 2.0;
    double maximumY = -2.0;
    for (const auto& point : path.points) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
    }
    CHECK_NEAR(minimumX, -1.0, 1e-6);
    CHECK_NEAR(maximumX, 1.0, 1e-6);
    CHECK_NEAR(minimumY, -0.5, 1e-6);
    CHECK_NEAR(maximumY, 0.5, 1e-6);
}

VWTEST("scope svg path commands and group transforms compose") {
    const std::string svg =
        "<svg><g transform=\"translate(10,20) scale(2)\">"
        "<path d=\"M0 0 L10 0 Q15 5 10 10 Z\"/>"
        "</g></svg>";
    ScopeDrawing drawing;
    std::string error;
    REQUIRE(parseScopeSvg(svg, drawing, {}, &error));
    REQUIRE(drawing.strokes.size() == size_t{1});
    const ScopePath& path = drawing.strokes.front();
    CHECK(path.closed);
    CHECK(path.points.size() > size_t{4});
    CHECK(near(path.points.front().x, -1.0, 1e-6));
    CHECK(near(path.points.front().y, 0.8, 1e-6));
}

VWTEST("scope svg parses shapes and multiple subpaths") {
    const std::string svg =
        "<svg>"
        "<circle cx=\"50\" cy=\"50\" r=\"20\"/>"
        "<ellipse cx=\"100\" cy=\"50\" rx=\"30\" ry=\"10\"/>"
        "<polyline points=\"0,0 10,10 20,0\"/>"
        "<polygon points=\"0,20 10,30 20,20\"/>"
        "<path d=\"M0 0 L5 0 M10 0 L15 0\"/>"
        "</svg>";
    ScopeDrawing drawing;
    std::string error;
    REQUIRE(parseScopeSvg(svg, drawing, {}, &error));
    CHECK(drawing.valid());
    CHECK_EQ(drawing.strokes.size(), size_t{6});
    CHECK(drawing.strokes[0].closed);
    CHECK(drawing.strokes[2].closed == false);
    CHECK(drawing.strokes[3].closed);
}

VWTEST("scope svg load and invalid input") {
    TempPath file(".svg");
    {
        std::ofstream out(file.path());
        out << "<svg><line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"10\"/></svg>";
    }
    ScopeDrawing drawing;
    std::string error;
    REQUIRE(loadScopeSvg(file.string(), drawing, {}, &error));
    CHECK(drawing.valid());
    CHECK_EQ(drawing.name, file.path().stem().string());

    ScopeDrawing invalid;
    error.clear();
    CHECK(!parseScopeSvg("<svg><text>no shapes</text></svg>", invalid,
                         {}, &error));
    CHECK(!error.empty());
}
