#include "vwave/ScopeTrace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct PixelPoint {
    int x = 0;
    int y = 0;
};

struct Edge {
    PixelPoint a;
    PixelPoint b;
    bool used = false;
};

uint64_t key(const PixelPoint& point) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(point.y)) << 32) |
           static_cast<uint32_t>(point.x);
}

PixelPoint direction(const Edge& edge) {
    return {edge.b.x - edge.a.x, edge.b.y - edge.a.y};
}

double directionAngle(const PixelPoint& direction) {
    return std::atan2(static_cast<double>(direction.y),
                      static_cast<double>(direction.x));
}

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::vector<PixelPoint> simplifyLoop(std::vector<PixelPoint> points,
                                     double tolerance) {
    if (points.size() < 3) return points;
    std::vector<PixelPoint> collinear;
    collinear.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        const PixelPoint& previous =
            points[(i + points.size() - 1) % points.size()];
        const PixelPoint& current = points[i];
        const PixelPoint& next = points[(i + 1) % points.size()];
        const int ax = current.x - previous.x;
        const int ay = current.y - previous.y;
        const int bx = next.x - current.x;
        const int by = next.y - current.y;
        if (static_cast<int64_t>(ax) * by -
                static_cast<int64_t>(ay) * bx !=
            0)
            collinear.push_back(current);
    }
    if (collinear.size() < 3) return collinear;

    const double toleranceSq = std::max(0.0, tolerance) * tolerance;
    if (toleranceSq <= 0.0) return collinear;
    std::vector<PixelPoint> result;
    result.push_back(collinear.front());
    for (size_t i = 1; i + 1 < collinear.size(); ++i) {
        const PixelPoint& a = result.back();
        const PixelPoint& b = collinear[i];
        const PixelPoint& c = collinear[i + 1];
        const double dx = static_cast<double>(c.x - a.x);
        const double dy = static_cast<double>(c.y - a.y);
        const double lengthSq = dx * dx + dy * dy;
        double distanceSq = 0.0;
        if (lengthSq > 1e-12) {
            const double cross =
                static_cast<double>(b.x - a.x) * dy -
                static_cast<double>(b.y - a.y) * dx;
            distanceSq = cross * cross / lengthSq;
        } else {
            const double bx = static_cast<double>(b.x - a.x);
            const double by = static_cast<double>(b.y - a.y);
            distanceSq = bx * bx + by * by;
        }
        if (distanceSq > toleranceSq) result.push_back(b);
    }
    result.push_back(collinear.back());
    return result;
}

void orderStrokes(std::vector<ScopePath>& strokes) {
    if (strokes.size() < 2) return;
    std::vector<ScopePath> ordered;
    ordered.reserve(strokes.size());
    ordered.push_back(std::move(strokes.front()));
    for (size_t i = 1; i < strokes.size(); ++i) {
        const ScopePoint cursor = ordered.back().points.back();
        size_t best = i;
        bool reverse = false;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (size_t j = i; j < strokes.size(); ++j) {
            const auto& candidate = strokes[j];
            if (candidate.points.empty()) continue;
            auto distanceSq = [&](const ScopePoint& point) {
                const double dx = point.x - cursor.x;
                const double dy = point.y - cursor.y;
                return dx * dx + dy * dy;
            };
            const double startDistance = distanceSq(candidate.points.front());
            const double endDistance = distanceSq(candidate.points.back());
            if (startDistance < bestDistance) {
                bestDistance = startDistance;
                best = j;
                reverse = false;
            }
            if (endDistance < bestDistance) {
                bestDistance = endDistance;
                best = j;
                reverse = true;
            }
        }
        ScopePath next = std::move(strokes[best]);
        if (reverse) std::reverse(next.points.begin(), next.points.end());
        ordered.push_back(std::move(next));
        std::swap(strokes[best], strokes[i]);
    }
    strokes = std::move(ordered);
}

} // namespace

ScopeTraceResult traceImageToScope(const ImageData& image,
                                   const ScopeTraceOptions& options,
                                   std::string* error) {
    if (error) error->clear();
    ScopeTraceResult result;
    result.sourceWidth = image.width;
    result.sourceHeight = image.height;
    if (!image.valid()) {
        setError(error, "cannot trace an invalid image");
        return result;
    }
    const int maxDimension = std::clamp(options.maxDimension, 16, 4096);
    const int sourceWidth = image.width;
    const int sourceHeight = image.height;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        setError(error, "image has invalid dimensions");
        return result;
    }
    const double scale = std::min(
        1.0, static_cast<double>(maxDimension) /
                 static_cast<double>(std::max(sourceWidth, sourceHeight)));
    const int width = std::max(4, std::min(maxDimension,
                                           static_cast<int>(std::llround(
                                               sourceWidth * scale))));
    const int height = std::max(4, std::min(maxDimension,
                                            static_cast<int>(std::llround(
                                                sourceHeight * scale))));
    result.width = width;
    result.height = height;

    const double gamma = std::clamp(options.gamma, 0.05, 8.0);
    const double contrast = std::clamp(options.contrast, 0.0, 8.0);
    const double threshold = std::clamp(options.threshold, 0.0, 1.0);
    std::vector<uint8_t> foreground(
        static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    int foregroundPixels = 0;
    for (int y = 0; y < height; ++y) {
        const int sourceY = std::clamp(
            static_cast<int>((static_cast<double>(y) + 0.5) *
                             static_cast<double>(sourceHeight) /
                             static_cast<double>(height)),
            0, sourceHeight - 1);
        for (int x = 0; x < width; ++x) {
            const int sourceX = std::clamp(
                static_cast<int>((static_cast<double>(x) + 0.5) *
                                 static_cast<double>(sourceWidth) /
                                 static_cast<double>(width)),
                0, sourceWidth - 1);
            const double red = static_cast<double>(
                image.at(sourceX, sourceY, 0)) /
                255.0;
            const double green = static_cast<double>(
                image.at(sourceX, sourceY,
                         std::min(1, image.channels - 1))) /
                255.0;
            const double blue = static_cast<double>(
                image.at(sourceX, sourceY,
                         std::min(2, image.channels - 1))) /
                255.0;
            double value = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            if (image.channels == 1) value = red;
            if (options.useAlpha && image.channels >= 4) {
                const double alpha = static_cast<double>(
                    image.at(sourceX, sourceY, 3)) /
                    255.0;
                value = value * alpha + (1.0 - alpha);
            }
            value = std::clamp((value - 0.5) * contrast + 0.5, 0.0, 1.0);
            value = std::pow(value, gamma);
            const bool isForeground =
                options.invert ? value > threshold : value < threshold;
            if (isForeground) {
                foreground[static_cast<size_t>(y) * width + x] = 1;
                ++foregroundPixels;
            }
        }
    }
    result.foregroundPixels = foregroundPixels;
    if (foregroundPixels == 0) {
        setError(error, "image has no foreground pixels after thresholding");
        return result;
    }

    auto at = [&](int x, int y) {
        return x >= 0 && x < width && y >= 0 && y < height &&
               foreground[static_cast<size_t>(y) * width + x] != 0;
    };

    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(foregroundPixels) * 2);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!at(x, y)) continue;
            if (!at(x, y - 1))
                edges.push_back({{x, y}, {x + 1, y}});
            if (!at(x + 1, y))
                edges.push_back({{x + 1, y}, {x + 1, y + 1}});
            if (!at(x, y + 1))
                edges.push_back({{x + 1, y + 1}, {x, y + 1}});
            if (!at(x - 1, y))
                edges.push_back({{x, y + 1}, {x, y}});
        }
    }
    if (edges.empty()) {
        setError(error, "image produced no traceable edges");
        return result;
    }

    std::unordered_map<uint64_t, std::vector<size_t>> outgoing;
    outgoing.reserve(edges.size() * 2);
    for (size_t i = 0; i < edges.size(); ++i)
        outgoing[key(edges[i].a)].push_back(i);

    std::vector<ScopePath> rawStrokes;
    for (size_t startEdge = 0; startEdge < edges.size(); ++startEdge) {
        if (edges[startEdge].used) continue;
        std::vector<PixelPoint> points;
        const PixelPoint start = edges[startEdge].a;
        size_t edgeIndex = startEdge;
        PixelPoint incoming{};
        for (size_t guard = 0; guard < edges.size() + 4; ++guard) {
            if (edges[edgeIndex].used) break;
            edges[edgeIndex].used = true;
            points.push_back(edges[edgeIndex].a);
            incoming = direction(edges[edgeIndex]);
            const PixelPoint current = edges[edgeIndex].b;
            if (current.x == start.x && current.y == start.y) break;
            const auto found = outgoing.find(key(current));
            if (found == outgoing.end()) break;
            size_t nextEdge = std::numeric_limits<size_t>::max();
            double bestTurn = std::numeric_limits<double>::infinity();
            for (const size_t candidate : found->second) {
                if (edges[candidate].used) continue;
                const PixelPoint candidateDirection =
                    direction(edges[candidate]);
                const double turn = std::fmod(
                    directionAngle(candidateDirection) -
                        directionAngle(incoming) + 2.0 * kPi,
                    2.0 * kPi);
                if (turn < bestTurn) {
                    bestTurn = turn;
                    nextEdge = candidate;
                }
            }
            if (nextEdge == std::numeric_limits<size_t>::max()) break;
            edgeIndex = nextEdge;
        }
        if (points.size() < 4) continue;
        points = simplifyLoop(std::move(points),
                              std::max(0.0, options.simplifyTolerance) *
                                  static_cast<double>(std::max(width, height)));
        if (points.size() < 3) continue;
        rawStrokes.emplace_back();
        rawStrokes.back().closed = true;
        rawStrokes.back().name = "Trace " +
                                 std::to_string(rawStrokes.size() + 1);
        rawStrokes.back().points.reserve(points.size());
        for (const auto& point : points)
            rawStrokes.back().points.push_back(
                {static_cast<double>(point.x),
                 static_cast<double>(point.y)});
    }
    if (rawStrokes.empty()) {
        setError(error, "image contours were too small to trace");
        return result;
    }

    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (const auto& stroke : rawStrokes) {
        for (const auto& point : stroke.points) {
            minimumX = std::min(minimumX, point.x);
            maximumX = std::max(maximumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumY = std::max(maximumY, point.y);
        }
    }
    const double spanX = std::max(1.0, maximumX - minimumX);
    const double spanY = std::max(1.0, maximumY - minimumY);
    const double normalization =
        2.0 / std::max(spanX, spanY);
    const double centerX = (minimumX + maximumX) * 0.5;
    const double centerY = (minimumY + maximumY) * 0.5;
    for (auto& stroke : rawStrokes) {
        for (auto& point : stroke.points) {
            point.x = (point.x - centerX) * normalization;
            point.y = -(point.y - centerY) * normalization;
        }
        if (stroke.points.size() >
            static_cast<size_t>(std::max(3, options.maxPointsPerStroke))) {
            const size_t stride = std::max<size_t>(
                1, stroke.points.size() /
                       static_cast<size_t>(options.maxPointsPerStroke));
            std::vector<ScopePoint> reduced;
            reduced.reserve(stroke.points.size() / stride + 1);
            for (size_t i = 0; i < stroke.points.size(); i += stride)
                reduced.push_back(stroke.points[i]);
            if (reduced.size() < 3) reduced = stroke.points;
            stroke.points = std::move(reduced);
        }
    }
    if (options.orderStrokes) orderStrokes(rawStrokes);
    if (rawStrokes.size() >
        static_cast<size_t>(std::max(1, options.maxStrokes)))
        rawStrokes.resize(static_cast<size_t>(options.maxStrokes));

    result.drawing.id = "traced";
    result.drawing.name = "Traced image";
    result.drawing.strokes = std::move(rawStrokes);
    return result;
}

} // namespace vwave
