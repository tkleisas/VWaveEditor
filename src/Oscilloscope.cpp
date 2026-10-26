#include "vwave/Oscilloscope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

struct PathSampler {
    const ScopePath* path = nullptr;
    std::vector<double> cumulative;
    double total = 0.0;

    explicit PathSampler(const ScopePath& source) : path(&source) {
        const size_t count = source.points.size();
        if (count < 2) return;
        cumulative.resize(count + (source.closed ? 1 : 0), 0.0);
        double length = 0.0;
        for (size_t i = 1; i < count; ++i) {
            const double dx = source.points[i].x - source.points[i - 1].x;
            const double dy = source.points[i].y - source.points[i - 1].y;
            length += std::hypot(dx, dy);
            cumulative[i] = length;
        }
        if (source.closed) {
            const double dx = source.points.front().x - source.points.back().x;
            const double dy = source.points.front().y - source.points.back().y;
            length += std::hypot(dx, dy);
            cumulative[count] = length;
        }
        total = length;
    }

    ScopePoint at(double position) const {
        if (!path || !path->valid()) return {};
        if (total <= 1e-12) return path->points.front();
        double u = path->closed ? position - std::floor(position)
                                : std::clamp(position, 0.0, 1.0);
        if (path->closed && u >= 1.0) u -= 1.0;
        if (u <= 0.0) return path->points.front();
        if (!path->closed && u >= 1.0) return path->points.back();
        const double target = u * total;
        const auto iterator =
            std::lower_bound(cumulative.begin(), cumulative.end(), target);
        size_t segment = static_cast<size_t>(
            std::max<std::ptrdiff_t>(0,
                (iterator - cumulative.begin()) - 1));
        const size_t pointCount = path->points.size();
        const size_t lastSegment = path->closed ? pointCount : pointCount - 1;
        segment = std::min(segment, lastSegment - 1);
        const size_t a = segment;
        const size_t b = (segment + 1) % pointCount;
        const double length = cumulative[segment + 1] - cumulative[segment];
        const double fraction =
            length > 1e-12 ? (target - cumulative[segment]) / length : 0.0;
        ScopePoint result;
        result.x = path->points[a].x +
                   (path->points[b].x - path->points[a].x) * fraction;
        result.y = path->points[a].y +
                   (path->points[b].y - path->points[a].y) * fraction;
        return result;
    }
};

struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double z1 = 0.0;
    double z2 = 0.0;

    void setLowPass(double sampleRate, double cutoff, double q) {
        const double clampedCutoff = std::clamp(
            cutoff, 10.0, std::max(10.0, sampleRate * 0.49));
        const double w0 = kTwoPi * clampedCutoff / sampleRate;
        const double cosine = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cosine) * 0.5 / a0;
        b1 = (1.0 - cosine) / a0;
        b2 = b0;
        a1 = -2.0 * cosine / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process(double input) {
        const double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

bool readJson(const std::string& file, nlohmann::json& root) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    in >> root;
    return true;
}

} // namespace

double ScopePath::length() const {
    if (points.size() < 2) return 0.0;
    double total = 0.0;
    for (size_t i = 1; i < points.size(); ++i)
        total += std::hypot(points[i].x - points[i - 1].x,
                            points[i].y - points[i - 1].y);
    if (closed)
        total += std::hypot(points.front().x - points.back().x,
                            points.front().y - points.back().y);
    return total;
}

const char* scopeShapeName(ScopeShape shape) {
    switch (shape) {
    case ScopeShape::Polyline: return "polyline";
    case ScopeShape::Line: return "line";
    case ScopeShape::Circle: return "circle";
    case ScopeShape::Ellipse: return "ellipse";
    case ScopeShape::Lissajous: return "lissajous";
    case ScopeShape::Spiral: return "spiral";
    case ScopeShape::FigureEight: return "figure_eight";
    }
    return "circle";
}

bool scopeShapeFromName(const std::string& name, ScopeShape& shape) {
    if (name == "polyline" || name == "path")
        shape = ScopeShape::Polyline;
    else if (name == "line") shape = ScopeShape::Line;
    else if (name == "circle") shape = ScopeShape::Circle;
    else if (name == "ellipse") shape = ScopeShape::Ellipse;
    else if (name == "lissajous") shape = ScopeShape::Lissajous;
    else if (name == "spiral") shape = ScopeShape::Spiral;
    else if (name == "figure_eight" || name == "figure-eight" ||
             name == "figure8")
        shape = ScopeShape::FigureEight;
    else return false;
    return true;
}

const char* scopeDirectionName(ScopeDirection direction) {
    switch (direction) {
    case ScopeDirection::Forward: return "forward";
    case ScopeDirection::Reverse: return "reverse";
    case ScopeDirection::PingPong: return "ping_pong";
    }
    return "forward";
}

bool scopeDirectionFromName(const std::string& name,
                            ScopeDirection& direction) {
    if (name == "forward") direction = ScopeDirection::Forward;
    else if (name == "reverse") direction = ScopeDirection::Reverse;
    else if (name == "ping_pong" || name == "ping-pong" || name == "pingpong")
        direction = ScopeDirection::PingPong;
    else return false;
    return true;
}

ScopePath makeScopeCircle(double radius, int points, std::string name) {
    ScopePath path;
    path.name = std::move(name);
    path.closed = true;
    const int count = std::clamp(points, 8, 8192);
    path.points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double angle =
            kTwoPi * static_cast<double>(i) / static_cast<double>(count);
        path.points.push_back({radius * std::cos(angle),
                               radius * std::sin(angle)});
    }
    return path;
}

ScopePath makeScopeEllipse(double radiusX, double radiusY, int points,
                           std::string name) {
    ScopePath path;
    path.name = std::move(name);
    path.closed = true;
    const int count = std::clamp(points, 8, 8192);
    path.points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double angle =
            kTwoPi * static_cast<double>(i) / static_cast<double>(count);
        path.points.push_back({radiusX * std::cos(angle),
                               radiusY * std::sin(angle)});
    }
    return path;
}

ScopePath makeScopeLissajous(double a, double b, double phase, double size,
                             int points, std::string name) {
    ScopePath path;
    path.name = std::move(name);
    path.closed = true;
    const int count = std::clamp(points, 16, 8192);
    path.points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double angle =
            kTwoPi * static_cast<double>(i) / static_cast<double>(count);
        path.points.push_back({size * std::sin(a * angle + phase),
                               size * std::sin(b * angle)});
    }
    return path;
}

ScopePath makeScopeSpiral(double size, int points, int turns,
                          std::string name) {
    ScopePath path;
    path.name = std::move(name);
    path.closed = false;
    const int count = std::clamp(points, 16, 8192);
    const int loops = std::clamp(turns, 1, 64);
    path.points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double t =
            static_cast<double>(i) / static_cast<double>(count - 1);
        const double radius = size * (0.05 + 0.95 * t);
        const double angle = kTwoPi * loops * t;
        path.points.push_back({radius * std::cos(angle),
                               radius * std::sin(angle)});
    }
    return path;
}

ScopePath makeScopeFigureEight(double size, int points, std::string name) {
    ScopePath path;
    path.name = std::move(name);
    path.closed = true;
    const int count = std::clamp(points, 16, 8192);
    path.points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double angle =
            kTwoPi * static_cast<double>(i) / static_cast<double>(count);
        path.points.push_back({size * std::sin(angle),
                               size * 0.6 * std::sin(2.0 * angle)});
    }
    return path;
}

ScopePath makeScopeLine(double size, std::string name) {
    ScopePath path;
    path.name = std::move(name);
    path.closed = false;
    path.points = {{-size, 0.0}, {size, 0.0}};
    return path;
}

ScopePath makeScopeShape(ScopeShape shape, int points, double size, double a,
                         double b, double phase, int turns,
                         std::string name) {
    if (name.empty()) name = scopeShapeName(shape);
    switch (shape) {
    case ScopeShape::Line:
        return makeScopeLine(size, std::move(name));
    case ScopeShape::Circle:
        return makeScopeCircle(size, points, std::move(name));
    case ScopeShape::Ellipse:
        return makeScopeEllipse(size, size * 0.62, points, std::move(name));
    case ScopeShape::Lissajous:
        return makeScopeLissajous(a, b, phase, size, points,
                                  std::move(name));
    case ScopeShape::Spiral:
        return makeScopeSpiral(size, points, turns, std::move(name));
    case ScopeShape::FigureEight:
        return makeScopeFigureEight(size, points, std::move(name));
    case ScopeShape::Polyline:
        break;
    }
    ScopePath path;
    path.name = std::move(name);
    path.closed = false;
    return path;
}

ScopePoint sampleScopePath(const ScopePath& path, double position) {
    PathSampler sampler(path);
    return sampler.at(position);
}

Audio renderScopePath(const ScopePath& path, const ScopeRenderOptions& options,
                      std::string* error) {
    if (error) error->clear();
    if (!path.valid()) {
        if (error) *error = "scope path needs at least two points";
        return Audio{};
    }
    if (options.sampleRate <= 0 || !(options.duration > 0.0) ||
        !std::isfinite(options.duration) || !(options.scanHz > 0.0) ||
        !std::isfinite(options.scanHz)) {
        if (error) *error = "invalid scope render options";
        return Audio{};
    }
    const int sampleRate = std::clamp(options.sampleRate, 1000, 192000);
    const double duration = std::clamp(options.duration, 0.01, 120.0);
    const double scanHz = std::clamp(
        options.scanHz, 0.1, static_cast<double>(sampleRate) * 0.49);
    const int64_t frames = static_cast<int64_t>(
        std::llround(duration * static_cast<double>(sampleRate)));
    if (frames <= 0) return Audio{};
    const int oversampling = std::clamp(options.oversampling, 1, 8);
    const int factor = options.bandLimited ? oversampling : 1;
    const double internalRate =
        static_cast<double>(sampleRate) * static_cast<double>(factor);
    const int64_t internalFrames = frames * static_cast<int64_t>(factor);
    const double gain = std::clamp(options.gain, -4.0, 4.0);
    const double smoothing = std::clamp(options.smoothing, 0.0, 0.99);
    const double cutoff = std::clamp(
        options.bandLimitHz, 20.0, internalRate * 0.49);
    Biquad leftFilter;
    Biquad rightFilter;
    if (options.bandLimited) {
        leftFilter.setLowPass(internalRate, cutoff, 0.7071067811865476);
        rightFilter.setLowPass(internalRate, cutoff, 0.7071067811865476);
    }
    PathSampler sampler(path);
    Audio output(2, frames, sampleRate);
    double smoothLeft = 0.0;
    double smoothRight = 0.0;
    for (int64_t internalFrame = 0; internalFrame < internalFrames;
         ++internalFrame) {
        const double time =
            static_cast<double>(internalFrame) / internalRate;
        double phase = std::fmod(time * scanHz, 1.0);
        if (phase < 0.0) phase += 1.0;
        double position = phase;
        if (options.direction == ScopeDirection::Reverse) {
            position = 1.0 - phase;
            if (position >= 1.0) position = 0.0;
        } else if (options.direction == ScopeDirection::PingPong) {
            const double cycle = std::fmod(time * scanHz * 2.0, 2.0);
            position = cycle <= 1.0 ? cycle : 2.0 - cycle;
        }
        const ScopePoint point = sampler.at(position);
        double left = std::clamp(point.x, -1.0, 1.0) * gain;
        double right = std::clamp(point.y, -1.0, 1.0) * gain;
        if (options.bandLimited) {
            left = leftFilter.process(left);
            right = rightFilter.process(right);
        }
        if ((internalFrame % factor) != 0) continue;
        const int64_t frame = internalFrame / factor;
        if (smoothing > 0.0) {
            smoothLeft = smoothLeft * smoothing + left * (1.0 - smoothing);
            smoothRight = smoothRight * smoothing + right * (1.0 - smoothing);
            left = smoothLeft;
            right = smoothRight;
        }
        output.sample(0, frame) = static_cast<float>(left);
        output.sample(1, frame) = static_cast<float>(right);
    }
    if (options.normalize) {
        double peak = 0.0;
        for (int64_t i = 0; i < frames; ++i) {
            peak = std::max(
                peak, static_cast<double>(std::abs(output.sample(0, i))));
            peak = std::max(
                peak, static_cast<double>(std::abs(output.sample(1, i))));
        }
        if (peak > 1e-12) {
            const double target = std::clamp(options.peak, 0.0, 1.0);
            const double scale = target / peak;
            for (int64_t i = 0; i < frames; ++i) {
                output.sample(0, i) = static_cast<float>(
                    output.sample(0, i) * scale);
                output.sample(1, i) = static_cast<float>(
                    output.sample(1, i) * scale);
            }
        }
    }
    return output;
}

bool saveScopePath(const ScopePath& path, const std::string& file,
                   std::string* error) {
    if (error) error->clear();
    if (!path.valid()) {
        if (error) *error = "cannot save an empty scope path";
        return false;
    }
    try {
        nlohmann::json root;
        root["format"] = "vwave-scope";
        root["version"] = 1;
        root["name"] = path.name;
        root["closed"] = path.closed;
        root["points"] = nlohmann::json::array();
        for (const auto& point : path.points)
            root["points"].push_back({point.x, point.y});
        std::ofstream out(file, std::ios::binary);
        if (!out) {
            if (error) *error = "cannot create scope path file";
            return false;
        }
        out << root.dump(2) << '\n';
        return out.good();
    } catch (const std::exception& e) {
        if (error) *error = std::string("cannot save scope path: ") +
                            e.what();
        return false;
    }
}

bool loadScopePath(const std::string& file, ScopePath& path,
                   std::string* error) {
    if (error) error->clear();
    try {
        nlohmann::json root;
        if (!readJson(file, root)) {
            if (error) *error = "cannot open scope path file";
            return false;
        }
        if (root.value("format", std::string{}) != "vwave-scope") {
            if (error) *error = "not a VWaveEditor scope path";
            return false;
        }
        if (root.value("version", 0) != 1) {
            if (error) *error = "unsupported scope path version";
            return false;
        }
        if (!root.contains("points") || !root["points"].is_array() ||
            root["points"].size() < 2) {
            if (error) *error = "scope path needs at least two points";
            return false;
        }
        ScopePath loaded;
        loaded.name = root.value("name", std::string{"Path"});
        loaded.closed = root.value("closed", true);
        for (const auto& point : root["points"]) {
            if (!point.is_array() || point.size() != 2 ||
                !point[0].is_number() || !point[1].is_number()) {
                if (error) *error = "invalid scope path point";
                return false;
            }
            const double x = point[0].get<double>();
            const double y = point[1].get<double>();
            if (!std::isfinite(x) || !std::isfinite(y)) {
                if (error) *error = "invalid scope path point";
                return false;
            }
            loaded.points.push_back({x, y});
        }
        if (!loaded.valid()) {
            if (error) *error = "invalid scope path";
            return false;
        }
        path = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("cannot load scope path: ") +
                            e.what();
        return false;
    }
}

} // namespace vwave
