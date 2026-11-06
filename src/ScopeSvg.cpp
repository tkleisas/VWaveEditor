#include "vwave/ScopeSvg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

struct Matrix {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};

Matrix multiply(const Matrix& first, const Matrix& second) {
    Matrix result;
    result.a = first.a * second.a + first.c * second.b;
    result.b = first.b * second.a + first.d * second.b;
    result.c = first.a * second.c + first.c * second.d;
    result.d = first.b * second.c + first.d * second.d;
    result.e = first.a * second.e + first.c * second.f + first.e;
    result.f = first.b * second.e + first.d * second.f + first.f;
    return result;
}

ScopePoint applyMatrix(const Matrix& matrix, const ScopePoint& point) {
    return {matrix.a * point.x + matrix.c * point.y + matrix.e,
            matrix.b * point.x + matrix.d * point.y + matrix.f};
}


std::string decodeEntities(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            result.push_back(text[i]);
            continue;
        }
        const size_t end = text.find(';', i + 1);
        if (end == std::string::npos) {
            result.push_back('&');
            continue;
        }
        const std::string entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") result.push_back('&');
        else if (entity == "lt") result.push_back('<');
        else if (entity == "gt") result.push_back('>');
        else if (entity == "quot") result.push_back('"');
        else if (entity == "apos") result.push_back('\'');
        else result += "&" + entity + ";";
        i = end;
    }
    return result;
}

std::string localName(const std::string& name) {
    const size_t colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

std::map<std::string, std::string> parseAttributes(
    const std::string& text, size_t& position, bool& selfClosing) {
    std::map<std::string, std::string> attributes;
    selfClosing = false;
    while (position < text.size()) {
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position])))
            ++position;
        if (position >= text.size()) break;
        if (text[position] == '>') {
            ++position;
            break;
        }
        if (text[position] == '/' && position + 1 < text.size() &&
            text[position + 1] == '>') {
            selfClosing = true;
            position += 2;
            break;
        }
        const size_t nameStart = position;
        while (position < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[position])) &&
               text[position] != '=' && text[position] != '>' &&
               text[position] != '/')
            ++position;
        const std::string name =
            localName(text.substr(nameStart, position - nameStart));
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position])))
            ++position;
        if (position >= text.size() || text[position] != '=') continue;
        ++position;
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position])))
            ++position;
        if (position >= text.size()) break;
        const char quote = text[position];
        if (quote != '"' && quote != '\'') {
            const size_t valueStart = position;
            while (position < text.size() &&
                   !std::isspace(static_cast<unsigned char>(text[position])) &&
                   text[position] != '>')
                ++position;
            attributes[name] =
                decodeEntities(text.substr(valueStart, position - valueStart));
            continue;
        }
        ++position;
        const size_t valueStart = position;
        while (position < text.size() && text[position] != quote) ++position;
        attributes[name] =
            decodeEntities(text.substr(valueStart, position - valueStart));
        if (position < text.size()) ++position;
    }
    return attributes;
}

std::vector<double> parseNumbers(const std::string& text) {
    std::vector<double> numbers;
    const char* cursor = text.c_str();
    const char* end = cursor + text.size();
    while (cursor < end) {
        while (cursor < end &&
               (std::isspace(static_cast<unsigned char>(*cursor)) ||
                *cursor == ','))
            ++cursor;
        if (cursor >= end) break;
        char* next = nullptr;
        const double value = std::strtod(cursor, &next);
        if (next == cursor) {
            ++cursor;
            continue;
        }
        numbers.push_back(value);
        cursor = next;
    }
    return numbers;
}

double numberAttribute(const std::map<std::string, std::string>& attributes,
                       const std::string& name, double fallback = 0.0) {
    const auto found = attributes.find(name);
    if (found == attributes.end()) return fallback;
    const auto numbers = parseNumbers(found->second);
    return numbers.empty() ? fallback : numbers.front();
}

Matrix parseTransform(const std::string& text) {
    Matrix result;
    size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[position])) ||
                text[position] == ','))
            ++position;
        const size_t nameStart = position;
        while (position < text.size() &&
               (std::isalpha(static_cast<unsigned char>(text[position]))))
            ++position;
        if (position == nameStart) break;
        const std::string name = text.substr(nameStart, position - nameStart);
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position])))
            ++position;
        if (position >= text.size() || text[position] != '(') break;
        const size_t argsStart = ++position;
        while (position < text.size() && text[position] != ')') ++position;
        const auto args =
            parseNumbers(text.substr(argsStart, position - argsStart));
        if (position < text.size()) ++position;
        Matrix local;
        if (name == "translate" && !args.empty()) {
            local.e = args[0];
            if (args.size() >= 2) local.f = args[1];
        } else if (name == "scale" && !args.empty()) {
            local.a = args[0];
            local.d = args.size() >= 2 ? args[1] : args[0];
        } else if (name == "rotate" && !args.empty()) {
            const double angle = args[0] * kPi / 180.0;
            local.a = std::cos(angle);
            local.b = std::sin(angle);
            local.c = -std::sin(angle);
            local.d = std::cos(angle);
            if (args.size() >= 3) {
                Matrix toCenter;
                toCenter.e = args[1];
                toCenter.f = args[2];
                Matrix fromCenter;
                fromCenter.e = -args[1];
                fromCenter.f = -args[2];
                local = multiply(multiply(toCenter, local), fromCenter);
            }
        } else if (name == "matrix" && args.size() >= 6) {
            local.a = args[0];
            local.b = args[1];
            local.c = args[2];
            local.d = args[3];
            local.e = args[4];
            local.f = args[5];
        } else if (name == "skewX" && !args.empty()) {
            local.c = std::tan(args[0] * kPi / 180.0);
        } else if (name == "skewY" && !args.empty()) {
            local.b = std::tan(args[0] * kPi / 180.0);
        }
        result = multiply(result, local);
    }
    return result;
}

struct PathParser {
    const std::string& data;
    const ScopeSvgOptions& options;
    size_t position = 0;
    char command = '\0';
    ScopePoint current;
    ScopePoint start;
    ScopePoint previousControl;
    ScopePoint previousQuadratic;
    bool hasPreviousCubic = false;
    bool hasPreviousQuadratic = false;
    std::vector<ScopePath> paths;
    ScopePath active;
    bool activeOpen = false;

    PathParser(const std::string& text, const ScopeSvgOptions& settings)
        : data(text), options(settings) {}

    void skip() {
        while (position < data.size() &&
               (std::isspace(static_cast<unsigned char>(data[position])) ||
                data[position] == ','))
            ++position;
    }

    bool number(double& value) {
        skip();
        if (position >= data.size()) return false;
        char* end = nullptr;
        value = std::strtod(data.c_str() + position, &end);
        if (end == data.c_str() + position) return false;
        position = static_cast<size_t>(end - data.c_str());
        return true;
    }

    bool readCommand() {
        skip();
        if (position >= data.size()) return false;
        if (std::isalpha(static_cast<unsigned char>(data[position]))) {
            command = data[position++];
            return true;
        }
        return command != '\0';
    }

    void finishActive() {
        if (active.points.size() >= 2) {
            active.name = "Path " + std::to_string(paths.size() + 1);
            paths.push_back(std::move(active));
        }
        active = ScopePath{};
        activeOpen = false;
    }

    void beginActive(ScopePoint point) {
        finishActive();
        active = ScopePath{};
        active.closed = false;
        active.points.push_back(point);
        activeOpen = true;
    }

    void addPoint(ScopePoint point) {
        active.points.push_back(point);
    }

    void lineTo(ScopePoint point) {
        if (!activeOpen) beginActive(current);
        addPoint(point);
        current = point;
    }

    void cubicTo(ScopePoint c1, ScopePoint c2, ScopePoint point) {
        if (!activeOpen) beginActive(current);
        const int segments = std::clamp(options.curveSegments, 2, 128);
        for (int i = 1; i <= segments; ++i) {
            const double t = static_cast<double>(i) /
                             static_cast<double>(segments);
            const double u = 1.0 - t;
            const double x = u * u * u * current.x +
                             3.0 * u * u * t * c1.x +
                             3.0 * u * t * t * c2.x + t * t * t * point.x;
            const double y = u * u * u * current.y +
                             3.0 * u * u * t * c1.y +
                             3.0 * u * t * t * c2.y + t * t * t * point.y;
            addPoint({x, y});
        }
        previousControl = c2;
        hasPreviousCubic = true;
        hasPreviousQuadratic = false;
        current = point;
    }

    void quadraticTo(ScopePoint control, ScopePoint point) {
        if (!activeOpen) beginActive(current);
        const int segments = std::clamp(options.curveSegments, 2, 128);
        for (int i = 1; i <= segments; ++i) {
            const double t = static_cast<double>(i) /
                             static_cast<double>(segments);
            const double u = 1.0 - t;
            const double x = u * u * current.x + 2.0 * u * t * control.x +
                             t * t * point.x;
            const double y = u * u * current.y + 2.0 * u * t * control.y +
                             t * t * point.y;
            addPoint({x, y});
        }
        previousQuadratic = control;
        hasPreviousQuadratic = true;
        hasPreviousCubic = false;
        current = point;
    }

    void arcTo(double rx, double ry, double rotation, bool largeArc,
               bool sweep, ScopePoint point) {
        rx = std::abs(rx);
        ry = std::abs(ry);
        if (rx < 1e-12 || ry < 1e-12) {
            lineTo(point);
            return;
        }
        const double phi = rotation * kPi / 180.0;
        const double cosine = std::cos(phi);
        const double sine = std::sin(phi);
        const double dx = (current.x - point.x) * 0.5;
        const double dy = (current.y - point.y) * 0.5;
        const double x1 = cosine * dx + sine * dy;
        const double y1 = -sine * dx + cosine * dy;
        double rxSq = rx * rx;
        double rySq = ry * ry;
        const double x1Sq = x1 * x1;
        const double y1Sq = y1 * y1;
        const double lambda = x1Sq / rxSq + y1Sq / rySq;
        if (lambda > 1.0) {
            const double scale = std::sqrt(lambda);
            rx *= scale;
            ry *= scale;
            rxSq = rx * rx;
            rySq = ry * ry;
        }
        const double sign = (largeArc != sweep) ? 1.0 : -1.0;
        const double denominator =
            std::max(1e-12, rxSq * y1Sq + rySq * x1Sq);
        double factor = std::sqrt(std::max(
            0.0, (rxSq * rySq - rxSq * y1Sq - rySq * x1Sq) / denominator));
        factor *= sign;
        const double cx1 = factor * rx * y1 / ry;
        const double cy1 = factor * -ry * x1 / rx;
        const double cx = cosine * cx1 - sine * cy1 +
                          (current.x + point.x) * 0.5;
        const double cy = sine * cx1 + cosine * cy1 +
                          (current.y + point.y) * 0.5;
        auto angle = [](double ux, double uy, double vx, double vy) {
            const double dot = ux * vx + uy * vy;
            const double lengths =
                std::max(1e-12, std::hypot(ux, uy) * std::hypot(vx, vy));
            double value = std::acos(std::clamp(dot / lengths, -1.0, 1.0));
            if (ux * vy - uy * vx < 0.0) value = -value;
            return value;
        };
        const double startAngle =
            angle(1.0, 0.0, (x1 - cx1) / rx, (y1 - cy1) / ry);
        double endAngle =
            angle((x1 - cx1) / rx, (y1 - cy1) / ry,
                  (-x1 - cx1) / rx, (-y1 - cy1) / ry);
        if (!sweep && endAngle > 0.0)
            endAngle -= kTwoPi;
        else if (sweep && endAngle < 0.0)
            endAngle += kTwoPi;
        const int segments = std::clamp(
            static_cast<int>(std::ceil(std::abs(endAngle) /
                                       (kTwoPi / 32.0))),
            2, std::max(2, options.curveSegments * 4));
        for (int i = 1; i <= segments; ++i) {
            const double theta =
                startAngle + endAngle * static_cast<double>(i) /
                                   static_cast<double>(segments);
            const double x = cx + cosine * rx * std::cos(theta) -
                             sine * ry * std::sin(theta);
            const double y = cy + sine * rx * std::cos(theta) +
                             cosine * ry * std::sin(theta);
            addPoint({x, y});
        }
        current = point;
        hasPreviousCubic = false;
        hasPreviousQuadratic = false;
    }

    std::vector<ScopePath> parse() {
        while (true) {
            if (!readCommand()) break;
            const bool relative = std::islower(
                static_cast<unsigned char>(command)) != 0;
            const char upper = static_cast<char>(
                std::toupper(static_cast<unsigned char>(command)));
            if (upper == 'Z') {
                if (activeOpen) {
                    active.closed = true;
                    finishActive();
                    current = start;
                }
                command = '\0';
                continue;
            }
            if (upper == 'M') {
                double x = 0.0;
                double y = 0.0;
                if (!number(x) || !number(y)) break;
                if (relative) {
                    x += current.x;
                    y += current.y;
                }
                beginActive({x, y});
                start = current = {x, y};
                command = relative ? 'l' : 'L';
                hasPreviousCubic = false;
                hasPreviousQuadratic = false;
                continue;
            }
            if (upper == 'L') {
                double x = 0.0;
                double y = 0.0;
                if (!number(x) || !number(y)) break;
                if (relative) {
                    x += current.x;
                    y += current.y;
                }
                lineTo({x, y});
                hasPreviousCubic = false;
                hasPreviousQuadratic = false;
                continue;
            }
            if (upper == 'H' || upper == 'V') {
                double value = 0.0;
                if (!number(value)) break;
                ScopePoint next = current;
                if (upper == 'H')
                    next.x = relative ? current.x + value : value;
                else
                    next.y = relative ? current.y + value : value;
                lineTo(next);
                hasPreviousCubic = false;
                hasPreviousQuadratic = false;
                continue;
            }
            if (upper == 'C') {
                double x1, y1, x2, y2, x, y;
                if (!number(x1) || !number(y1) || !number(x2) ||
                    !number(y2) || !number(x) || !number(y))
                    break;
                if (relative) {
                    x1 += current.x; y1 += current.y;
                    x2 += current.x; y2 += current.y;
                    x += current.x; y += current.y;
                }
                cubicTo({x1, y1}, {x2, y2}, {x, y});
                continue;
            }
            if (upper == 'S') {
                double x2, y2, x, y;
                if (!number(x2) || !number(y2) || !number(x) || !number(y))
                    break;
                if (relative) {
                    x2 += current.x; y2 += current.y;
                    x += current.x; y += current.y;
                }
                const ScopePoint c1 =
                    hasPreviousCubic
                        ? ScopePoint{2.0 * current.x - previousControl.x,
                                     2.0 * current.y - previousControl.y}
                        : current;
                cubicTo(c1, {x2, y2}, {x, y});
                continue;
            }
            if (upper == 'Q') {
                double x1, y1, x, y;
                if (!number(x1) || !number(y1) || !number(x) || !number(y))
                    break;
                if (relative) {
                    x1 += current.x; y1 += current.y;
                    x += current.x; y += current.y;
                }
                quadraticTo({x1, y1}, {x, y});
                continue;
            }
            if (upper == 'T') {
                double x, y;
                if (!number(x) || !number(y)) break;
                if (relative) {
                    x += current.x; y += current.y;
                }
                const ScopePoint control =
                    hasPreviousQuadratic
                        ? ScopePoint{2.0 * current.x - previousQuadratic.x,
                                     2.0 * current.y - previousQuadratic.y}
                        : current;
                quadraticTo(control, {x, y});
                continue;
            }
            if (upper == 'A') {
                double rx, ry, rotation, largeArc, sweep, x, y;
                if (!number(rx) || !number(ry) || !number(rotation) ||
                    !number(largeArc) || !number(sweep) || !number(x) ||
                    !number(y))
                    break;
                if (relative) {
                    x += current.x;
                    y += current.y;
                }
                arcTo(rx, ry, rotation, largeArc != 0.0, sweep != 0.0,
                      {x, y});
                continue;
            }
            break;
        }
        finishActive();
        return paths;
    }
};

double distanceSquared(const ScopePoint& a, const ScopePoint& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

ScopePath simplifyPath(const ScopePath& path, double tolerance,
                       int maxPoints) {
    if (path.points.size() <= 2) return path;
    const double toleranceSq = tolerance * tolerance;
    std::vector<ScopePoint> result;
    result.push_back(path.points.front());
    for (size_t i = 1; i + 1 < path.points.size(); ++i) {
        const ScopePoint& a = result.back();
        const ScopePoint& b = path.points[i];
        const ScopePoint& c = path.points[i + 1];
        const double dx = c.x - a.x;
        const double dy = c.y - a.y;
        const double lengthSq = dx * dx + dy * dy;
        double distanceSq = distanceSquared(a, b);
        if (lengthSq > 1e-12) {
            const double cross = (b.x - a.x) * dy - (b.y - a.y) * dx;
            distanceSq = cross * cross / lengthSq;
        }
        if (distanceSq > toleranceSq) result.push_back(b);
    }
    result.push_back(path.points.back());
    ScopePath simplified = path;
    simplified.points = std::move(result);
    if (maxPoints > 0 &&
        simplified.points.size() > static_cast<size_t>(maxPoints)) {
        const size_t stride =
            std::max<size_t>(1, simplified.points.size() /
                                  static_cast<size_t>(maxPoints));
        std::vector<ScopePoint> reduced;
        for (size_t i = 0; i < simplified.points.size(); i += stride)
            reduced.push_back(simplified.points[i]);
        if (reduced.size() < 2) reduced = simplified.points;
        simplified.points = std::move(reduced);
    }
    return simplified;
}

std::vector<ScopePath> shapePaths(
    const std::string& name,
    const std::map<std::string, std::string>& attributes,
    const ScopeSvgOptions& options) {
    std::vector<ScopePath> paths;
    const int segments = std::clamp(options.circleSegments, 8, 512);
    if (name == "path" && attributes.count("d")) {
        PathParser parser(attributes.at("d"), options);
        paths = parser.parse();
    } else if (name == "line") {
        ScopePath path;
        path.points = {
            {numberAttribute(attributes, "x1"),
             numberAttribute(attributes, "y1")},
            {numberAttribute(attributes, "x2"),
             numberAttribute(attributes, "y2")}};
        path.name = "Line";
        paths.push_back(std::move(path));
    } else if (name == "polyline" || name == "polygon") {
        const auto numbers = parseNumbers(attributes.count("points")
                                              ? attributes.at("points")
                                              : std::string{});
        ScopePath path;
        path.closed = name == "polygon";
        for (size_t i = 0; i + 1 < numbers.size(); i += 2)
            path.points.push_back({numbers[i], numbers[i + 1]});
        if (path.points.size() >= 2) {
            path.name = name == "polygon" ? "Polygon" : "Polyline";
            paths.push_back(std::move(path));
        }
    } else if (name == "rect") {
        const double x = numberAttribute(attributes, "x");
        const double y = numberAttribute(attributes, "y");
        const double width = numberAttribute(attributes, "width");
        const double height = numberAttribute(attributes, "height");
        if (width > 0.0 && height > 0.0) {
            ScopePath path;
            path.closed = true;
            path.name = "Rectangle";
            path.points = {{x, y}, {x + width, y},
                           {x + width, y + height}, {x, y + height}};
            paths.push_back(std::move(path));
        }
    } else if (name == "circle" || name == "ellipse") {
        const double cx = numberAttribute(attributes, "cx");
        const double cy = numberAttribute(attributes, "cy");
        double rx = numberAttribute(attributes, "r");
        double ry = rx;
        if (name == "ellipse") {
            rx = numberAttribute(attributes, "rx");
            ry = numberAttribute(attributes, "ry");
        }
        if (rx > 0.0 && ry > 0.0) {
            ScopePath path;
            path.closed = true;
            path.name = name == "circle" ? "Circle" : "Ellipse";
            for (int i = 0; i < segments; ++i) {
                const double angle =
                    kTwoPi * static_cast<double>(i) /
                    static_cast<double>(segments);
                path.points.push_back(
                    {cx + rx * std::cos(angle),
                     cy + ry * std::sin(angle)});
            }
            paths.push_back(std::move(path));
        }
    }
    return paths;
}

bool isSkippedContainer(const std::string& name) {
    return name == "defs" || name == "clipPath" || name == "mask" ||
           name == "pattern" || name == "symbol" || name == "marker" ||
           name == "text" || name == "style" || name == "title" ||
           name == "desc" || name == "metadata";
}

bool parseSvgText(const std::string& text, ScopeDrawing& drawing,
                  const ScopeSvgOptions& options, std::string* error) {
    struct Context {
        Matrix matrix;
        bool skip = false;
    };
    std::vector<Context> stack;
    stack.push_back({{}, false});
    size_t position = 0;
    while (position < text.size()) {
        const size_t open = text.find('<', position);
        if (open == std::string::npos) break;
        if (text.compare(open, 4, "<!--") == 0) {
            const size_t end = text.find("-->", open + 4);
            position = end == std::string::npos ? text.size() : end + 3;
            continue;
        }
        if (text.compare(open, 2, "<?") == 0) {
            const size_t end = text.find("?>", open + 2);
            position = end == std::string::npos ? text.size() : end + 2;
            continue;
        }
        if (text.compare(open, 2, "<!") == 0) {
            const size_t end = text.find('>', open + 2);
            position = end == std::string::npos ? text.size() : end + 1;
            continue;
        }
        const bool closing =
            open + 1 < text.size() && text[open + 1] == '/';
        size_t cursor = open + (closing ? 2 : 1);
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])))
            ++cursor;
        const size_t nameStart = cursor;
        while (cursor < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[cursor])) &&
               text[cursor] != '>' && text[cursor] != '/')
            ++cursor;
        const std::string name =
            localName(text.substr(nameStart, cursor - nameStart));
        if (closing) {
            if (stack.size() > 1) stack.pop_back();
            const size_t end = text.find('>', cursor);
            position = end == std::string::npos ? text.size() : end + 1;
            continue;
        }
        bool selfClosing = false;
        const auto attributes = parseAttributes(text, cursor, selfClosing);
        position = cursor;
        Context current = stack.back();
        const bool skip = current.skip || isSkippedContainer(name);
        Matrix local = current.matrix;
        if (!skip && attributes.count("transform"))
            local = multiply(current.matrix,
                             parseTransform(attributes.at("transform")));
        if (!skip) {
            const auto paths = shapePaths(name, attributes, options);
            for (auto path : paths) {
                for (auto& point : path.points)
                    point = applyMatrix(local, point);
                if (path.points.size() >= 2)
                    drawing.strokes.push_back(std::move(path));
            }
        }
        if (!selfClosing)
            stack.push_back({local, skip});
    }
    if (drawing.strokes.empty()) {
        setError(error, "SVG contains no supported shapes");
        return false;
    }
    if (options.normalize) {
        double minimumX = std::numeric_limits<double>::infinity();
        double maximumX = -std::numeric_limits<double>::infinity();
        double minimumY = std::numeric_limits<double>::infinity();
        double maximumY = -std::numeric_limits<double>::infinity();
        for (const auto& stroke : drawing.strokes) {
            for (const auto& point : stroke.points) {
                minimumX = std::min(minimumX, point.x);
                maximumX = std::max(maximumX, point.x);
                minimumY = std::min(minimumY, point.y);
                maximumY = std::max(maximumY, point.y);
            }
        }
        const double span = std::max(
            1e-9, std::max(maximumX - minimumX, maximumY - minimumY));
        const double scale = 2.0 / span;
        const double centerX = (minimumX + maximumX) * 0.5;
        const double centerY = (minimumY + maximumY) * 0.5;
        for (auto& stroke : drawing.strokes) {
            for (auto& point : stroke.points) {
                point.x = (point.x - centerX) * scale;
                point.y = -(point.y - centerY) * scale;
            }
        }
    }
    for (auto& stroke : drawing.strokes) {
        stroke = simplifyPath(stroke, options.simplifyTolerance, 0);
        if (stroke.points.size() < 2) stroke.points.clear();
    }
    drawing.strokes.erase(
        std::remove_if(drawing.strokes.begin(), drawing.strokes.end(),
                       [](const ScopePath& path) {
                           return path.points.size() < 2;
                       }),
        drawing.strokes.end());
    if (drawing.strokes.empty()) {
        setError(error, "SVG shapes were too small to trace");
        return false;
    }
    return true;
}

} // namespace

bool parseScopeSvg(const std::string& text, ScopeDrawing& drawing,
                   const ScopeSvgOptions& options, std::string* error) {
    if (error) error->clear();
    ScopeDrawing loaded;
    loaded.id = "svg";
    loaded.name = "SVG";
    if (!parseSvgText(text, loaded, options, error)) return false;
    drawing = std::move(loaded);
    return true;
}

bool loadScopeSvg(const std::string& file, ScopeDrawing& drawing,
                  const ScopeSvgOptions& options, std::string* error) {
    if (error) error->clear();
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        setError(error, "cannot open SVG file");
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!parseScopeSvg(buffer.str(), drawing, options, error)) return false;
    const size_t slash = file.find_last_of("/\\");
    const size_t dot = file.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        drawing.name = file.substr(slash == std::string::npos ? 0 : slash + 1,
                                   dot - (slash == std::string::npos ? 0
                                                                     : slash + 1));
    return true;
}

} // namespace vwave
