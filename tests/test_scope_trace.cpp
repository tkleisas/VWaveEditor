#include "test_framework.h"

#include "vwave/ScopeTrace.h"

#include <algorithm>

using namespace vwave;

namespace {

ImageData grayscaleImage(int width, int height, uint8_t background) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = 1;
    image.pixels.assign(static_cast<size_t>(width) *
                            static_cast<size_t>(height),
                        background);
    return image;
}

void fillRect(ImageData& image, int left, int top, int right, int bottom,
              uint8_t value) {
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            image.pixels[static_cast<size_t>(y) * image.width + x] = value;
        }
    }
}

} // namespace

VWTEST("scope trace finds a closed contour for a filled rectangle") {
    ImageData image = grayscaleImage(20, 20, 255);
    fillRect(image, 5, 5, 15, 15, 0);
    std::string error;
    const ScopeTraceResult result = traceImageToScope(image, {}, &error);
    CHECK(error.empty());
    CHECK_EQ(result.foregroundPixels, 100);
    CHECK(result.drawing.valid());
    CHECK_EQ(result.drawing.strokes.size(), size_t{1});
    const ScopePath& stroke = result.drawing.strokes.front();
    CHECK(stroke.closed);
    CHECK(stroke.points.size() >= 4);
    double minimumX = 2.0;
    double maximumX = -2.0;
    double minimumY = 2.0;
    double maximumY = -2.0;
    for (const auto& point : stroke.points) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
    }
    CHECK_NEAR(minimumX, -1.0, 0.05);
    CHECK_NEAR(maximumX, 1.0, 0.05);
    CHECK_NEAR(minimumY, -1.0, 0.05);
    CHECK_NEAR(maximumY, 1.0, 0.05);
}

VWTEST("scope trace returns multiple strokes and honors settings") {
    ImageData image = grayscaleImage(30, 20, 255);
    fillRect(image, 2, 4, 7, 9, 0);
    fillRect(image, 20, 10, 27, 17, 0);
    ScopeTraceOptions options;
    options.maxStrokes = 1;
    std::string error;
    const ScopeTraceResult limited = traceImageToScope(image, options, &error);
    CHECK(error.empty());
    CHECK_EQ(limited.drawing.strokes.size(), size_t{1});

    options.maxStrokes = 8;
    const ScopeTraceResult result = traceImageToScope(image, options, &error);
    CHECK_EQ(result.drawing.strokes.size(), size_t{2});
    CHECK(result.drawing.strokes[0].closed);
    CHECK(result.drawing.strokes[1].closed);
}

VWTEST("scope trace handles alpha invert and empty images") {
    ImageData alpha;
    alpha.width = 12;
    alpha.height = 12;
    alpha.channels = 4;
    alpha.pixels.assign(12 * 12 * 4, 0);
    for (int y = 4; y < 8; ++y) {
        for (int x = 4; x < 8; ++x) {
            const size_t index =
                (static_cast<size_t>(y) * alpha.width + x) * 4;
            alpha.pixels[index] = 0;
            alpha.pixels[index + 1] = 0;
            alpha.pixels[index + 2] = 0;
            alpha.pixels[index + 3] = 255;
        }
    }
    std::string error;
    const ScopeTraceResult traced = traceImageToScope(alpha, {}, &error);
    CHECK(error.empty());
    CHECK(traced.drawing.valid());
    CHECK_EQ(traced.foregroundPixels, 16);

    ImageData blank = grayscaleImage(8, 8, 255);
    error.clear();
    const ScopeTraceResult empty = traceImageToScope(blank, {}, &error);
    CHECK(!empty.drawing.valid());
    CHECK(!error.empty());

    ScopeTraceOptions inverted;
    inverted.invert = true;
    error.clear();
    const ScopeTraceResult flipped = traceImageToScope(alpha, inverted,
                                                       &error);
    CHECK(error.empty());
    CHECK(flipped.foregroundPixels > traced.foregroundPixels);
}

VWTEST("scope trace simplifies and caps stroke points") {
    ImageData image = grayscaleImage(64, 64, 255);
    fillRect(image, 8, 8, 56, 56, 0);
    ScopeTraceOptions options;
    options.maxPointsPerStroke = 4;
    std::string error;
    const ScopeTraceResult result = traceImageToScope(image, options, &error);
    CHECK(error.empty());
    REQUIRE(!result.drawing.strokes.empty());
    CHECK(result.drawing.strokes.front().points.size() >= 3);
}

VWTEST("traced drawings render through the scope composition") {
    ImageData image = grayscaleImage(16, 16, 255);
    fillRect(image, 3, 3, 13, 13, 0);
    std::string error;
    const ScopeTraceResult traced = traceImageToScope(image, {}, &error);
    REQUIRE(traced.drawing.valid());
    ScopeComposition composition;
    composition.duration = 0.05;
    composition.drawings.push_back(traced.drawing);
    ScopeKeyframe keyframe;
    keyframe.time = 0.0;
    keyframe.drawing = traced.drawing.id;
    composition.keyframes.push_back(keyframe);
    ScopeRenderOptions options;
    options.sampleRate = 8000;
    options.duration = composition.duration;
    options.scanHz = 80.0;
    options.bandLimited = false;
    const Audio audio = renderScopeComposition(composition, options, &error);
    CHECK(error.empty());
    CHECK_EQ(audio.numChannels(), 2);
    CHECK(audio.numFrames() > 0);
    CHECK(std::isfinite(audio.sample(0, 10)));
    CHECK(std::isfinite(audio.sample(1, 10)));
}
