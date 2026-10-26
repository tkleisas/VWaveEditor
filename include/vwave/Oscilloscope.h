#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

struct ScopePoint {
    double x = 0.0;
    double y = 0.0;
};

struct ScopePath {
    std::string name;
    bool closed = true;
    std::vector<ScopePoint> points;

    bool valid() const { return points.size() >= 2; }
    double length() const;
};

enum class ScopeShape {
    Polyline,
    Line,
    Circle,
    Ellipse,
    Lissajous,
    Spiral,
    FigureEight,
};

const char* scopeShapeName(ScopeShape shape);
bool scopeShapeFromName(const std::string& name, ScopeShape& shape);

enum class ScopeDirection {
    Forward,
    Reverse,
    PingPong,
};

const char* scopeDirectionName(ScopeDirection direction);
bool scopeDirectionFromName(const std::string& name,
                            ScopeDirection& direction);

ScopePath makeScopeCircle(double radius = 0.8, int points = 256,
                          std::string name = "Circle");
ScopePath makeScopeEllipse(double radiusX = 0.8, double radiusY = 0.5,
                           int points = 256, std::string name = "Ellipse");
ScopePath makeScopeLissajous(double a = 3.0, double b = 2.0,
                             double phase = 0.0, double size = 0.8,
                             int points = 512,
                             std::string name = "Lissajous");
ScopePath makeScopeSpiral(double size = 0.8, int points = 512,
                          int turns = 4, std::string name = "Spiral");
ScopePath makeScopeFigureEight(double size = 0.8, int points = 512,
                               std::string name = "Figure Eight");
ScopePath makeScopeLine(double size = 0.8, std::string name = "Line");
ScopePath makeScopeShape(ScopeShape shape, int points = 256, double size = 0.8,
                         double a = 3.0, double b = 2.0, double phase = 0.0,
                         int turns = 4, std::string name = {});

ScopePoint sampleScopePath(const ScopePath& path, double position);

struct ScopeRenderOptions {
    int sampleRate = 48000;
    double duration = 2.0;
    double scanHz = 120.0;
    double gain = 0.8;
    double smoothing = 0.0;
    bool bandLimited = true;
    double bandLimitHz = 16000.0;
    int oversampling = 4;
    ScopeDirection direction = ScopeDirection::Forward;
    bool normalize = true;
    double peak = 0.9;
};

Audio renderScopePath(const ScopePath& path, const ScopeRenderOptions& options,
                      std::string* error = nullptr);

bool saveScopePath(const ScopePath& path, const std::string& file,
                   std::string* error = nullptr);
bool loadScopePath(const std::string& file, ScopePath& path,
                   std::string* error = nullptr);

} // namespace vwave
