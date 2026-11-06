#include "vwave/ScopeComposition.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <string>
#include <utility>

namespace vwave {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool finite(double value) { return std::isfinite(value); }

nlohmann::json pathToJson(const ScopePath& path) {
    nlohmann::json json;
    json["name"] = path.name;
    json["closed"] = path.closed;
    json["points"] = nlohmann::json::array();
    for (const auto& point : path.points)
        json["points"].push_back({point.x, point.y});
    return json;
}

bool pathFromJson(const nlohmann::json& json, ScopePath& path,
                  std::string* error) {
    if (!json.is_object() || !json.contains("points") ||
        !json["points"].is_array() || json["points"].size() < 2) {
        setError(error, "scope path needs at least two points");
        return false;
    }
    ScopePath loaded;
    loaded.name = json.value("name", std::string{"Path"});
    loaded.closed = json.value("closed", true);
    for (const auto& point : json["points"]) {
        if (!point.is_array() || point.size() != 2 ||
            !point[0].is_number() || !point[1].is_number()) {
            setError(error, "invalid scope path point");
            return false;
        }
        const double x = point[0].get<double>();
        const double y = point[1].get<double>();
        if (!finite(x) || !finite(y)) {
            setError(error, "invalid scope path point");
            return false;
        }
        loaded.points.push_back({x, y});
    }
    if (!loaded.valid()) {
        setError(error, "invalid scope path");
        return false;
    }
    path = std::move(loaded);
    return true;
}

nlohmann::json transformToJson(const ScopeTransform& transform) {
    return {{"translateX", transform.translateX},
            {"translateY", transform.translateY},
            {"scaleX", transform.scaleX},
            {"scaleY", transform.scaleY},
            {"rotation", transform.rotation},
            {"pivotX", transform.pivotX},
            {"pivotY", transform.pivotY}};
}

bool transformFromJson(const nlohmann::json& json,
                       ScopeTransform& transform, std::string* error) {
    if (!json.is_object()) {
        setError(error, "scope transform must be an object");
        return false;
    }
    ScopeTransform loaded;
    loaded.translateX = json.value("translateX", 0.0);
    loaded.translateY = json.value("translateY", 0.0);
    loaded.scaleX = json.value("scaleX", 1.0);
    loaded.scaleY = json.value("scaleY", 1.0);
    loaded.rotation = json.value("rotation", 0.0);
    loaded.pivotX = json.value("pivotX", 0.0);
    loaded.pivotY = json.value("pivotY", 0.0);
    if (!finite(loaded.translateX) || !finite(loaded.translateY) ||
        !finite(loaded.scaleX) || !finite(loaded.scaleY) ||
        !finite(loaded.rotation) || !finite(loaded.pivotX) ||
        !finite(loaded.pivotY)) {
        setError(error, "scope transform contains non-finite values");
        return false;
    }
    transform = loaded;
    return true;
}

} // namespace

bool ScopeDrawing::valid() const {
    if (id.empty()) return false;
    for (const auto& stroke : strokes) {
        if (stroke.valid()) return true;
    }
    return false;
}

ScopePath ScopeDrawing::flatten() const {
    ScopePath result;
    result.name = name;
    result.closed = false;
    for (const auto& stroke : strokes) {
        if (!stroke.valid()) continue;
        result.points.insert(result.points.end(), stroke.points.begin(),
                             stroke.points.end());
        if (stroke.closed && stroke.points.size() > 1)
            result.points.push_back(stroke.points.front());
    }
    return result;
}

const char* scopeEasingName(ScopeEasing easing) {
    switch (easing) {
    case ScopeEasing::Hold: return "hold";
    case ScopeEasing::Linear: return "linear";
    case ScopeEasing::EaseIn: return "ease_in";
    case ScopeEasing::EaseOut: return "ease_out";
    case ScopeEasing::EaseInOut: return "ease_in_out";
    case ScopeEasing::Smoothstep: return "smoothstep";
    }
    return "linear";
}

bool scopeEasingFromName(const std::string& name, ScopeEasing& easing) {
    if (name == "hold" || name == "step") easing = ScopeEasing::Hold;
    else if (name == "linear") easing = ScopeEasing::Linear;
    else if (name == "ease_in" || name == "ease-in")
        easing = ScopeEasing::EaseIn;
    else if (name == "ease_out" || name == "ease-out")
        easing = ScopeEasing::EaseOut;
    else if (name == "ease_in_out" || name == "ease-in-out")
        easing = ScopeEasing::EaseInOut;
    else if (name == "smoothstep") easing = ScopeEasing::Smoothstep;
    else return false;
    return true;
}

const char* scopeTransitionName(ScopeTransition transition) {
    switch (transition) {
    case ScopeTransition::Hold: return "hold";
    case ScopeTransition::Crossfade: return "crossfade";
    }
    return "hold";
}

bool scopeTransitionFromName(const std::string& name,
                             ScopeTransition& transition) {
    if (name == "hold" || name == "cut") transition = ScopeTransition::Hold;
    else if (name == "crossfade" || name == "blend")
        transition = ScopeTransition::Crossfade;
    else return false;
    return true;
}

double scopeEasingValue(ScopeEasing easing, double progress) {
    const double p = std::clamp(progress, 0.0, 1.0);
    switch (easing) {
    case ScopeEasing::Hold: return p >= 1.0 ? 1.0 : 0.0;
    case ScopeEasing::Linear: return p;
    case ScopeEasing::EaseIn: return p * p;
    case ScopeEasing::EaseOut: return 1.0 - (1.0 - p) * (1.0 - p);
    case ScopeEasing::EaseInOut:
        return p < 0.5 ? 2.0 * p * p
                       : 1.0 - 2.0 * (1.0 - p) * (1.0 - p);
    case ScopeEasing::Smoothstep: return p * p * (3.0 - 2.0 * p);
    }
    return p;
}

ScopePoint applyScopeTransform(const ScopeTransform& transform,
                               ScopePoint point) {
    double x = point.x - transform.pivotX;
    double y = point.y - transform.pivotY;
    x *= transform.scaleX;
    y *= transform.scaleY;
    const double cosine = std::cos(transform.rotation);
    const double sine = std::sin(transform.rotation);
    const double rotatedX = x * cosine - y * sine;
    const double rotatedY = x * sine + y * cosine;
    return {rotatedX + transform.pivotX + transform.translateX,
            rotatedY + transform.pivotY + transform.translateY};
}

ScopeTransform interpolateScopeTransform(const ScopeTransform& a,
                                         const ScopeTransform& b,
                                         double progress) {
    const double p = std::clamp(progress, 0.0, 1.0);
    auto mix = [p](double from, double to) {
        return from + (to - from) * p;
    };
    ScopeTransform result;
    result.translateX = mix(a.translateX, b.translateX);
    result.translateY = mix(a.translateY, b.translateY);
    result.scaleX = mix(a.scaleX, b.scaleX);
    result.scaleY = mix(a.scaleY, b.scaleY);
    result.pivotX = mix(a.pivotX, b.pivotX);
    result.pivotY = mix(a.pivotY, b.pivotY);
    result.rotation =
        a.rotation + std::remainder(b.rotation - a.rotation, kTwoPi) * p;
    return result;
}

bool ScopeComposition::valid() const { return validate(nullptr); }

const ScopeDrawing* ScopeComposition::findDrawing(
    const std::string& drawingId) const {
    for (const auto& drawing : drawings) {
        if (drawing.id == drawingId) return &drawing;
    }
    return nullptr;
}

bool ScopeComposition::validate(std::string* error) const {
    if (error) error->clear();
    if (!finite(duration) || duration <= 0.0) {
        setError(error, "scope composition duration must be positive");
        return false;
    }
    if (drawings.empty()) {
        setError(error, "scope composition has no drawings");
        return false;
    }
    if (keyframes.empty()) {
        setError(error, "scope composition has no keyframes");
        return false;
    }
    std::set<std::string> ids;
    for (const auto& drawing : drawings) {
        if (!drawing.valid()) {
            setError(error, "scope drawing is empty or has no id");
            return false;
        }
        if (!ids.insert(drawing.id).second) {
            setError(error, "duplicate scope drawing id");
            return false;
        }
    }
    double previousTime = -1.0;
    for (const auto& keyframe : keyframes) {
        if (!finite(keyframe.time) || keyframe.time < 0.0) {
            setError(error, "scope keyframe time is invalid");
            return false;
        }
        if (keyframe.time < previousTime) {
            setError(error, "scope keyframes are not sorted");
            return false;
        }
        previousTime = keyframe.time;
        if (ids.find(keyframe.drawing) == ids.end()) {
            setError(error, "scope keyframe references a missing drawing");
            return false;
        }
        if (!finite(keyframe.scanHz) || keyframe.scanHz < 0.0) {
            setError(error, "scope keyframe scan rate is invalid");
            return false;
        }
        if (!finite(keyframe.gain) || !finite(keyframe.transform.translateX) ||
            !finite(keyframe.transform.translateY) ||
            !finite(keyframe.transform.scaleX) ||
            !finite(keyframe.transform.scaleY) ||
            !finite(keyframe.transform.rotation) ||
            !finite(keyframe.transform.pivotX) ||
            !finite(keyframe.transform.pivotY)) {
            setError(error, "scope keyframe contains non-finite values");
            return false;
        }
    }
    return true;
}

nlohmann::json scopeCompositionToJson(const ScopeComposition& composition) {
    nlohmann::json root;
    root["format"] = "vwave-scope";
    root["version"] = 2;
    root["name"] = composition.name;
    root["duration"] = composition.duration;
    root["drawings"] = nlohmann::json::array();
    for (const auto& drawing : composition.drawings) {
        nlohmann::json item;
        item["id"] = drawing.id;
        item["name"] = drawing.name;
        item["strokes"] = nlohmann::json::array();
        for (const auto& stroke : drawing.strokes)
            item["strokes"].push_back(pathToJson(stroke));
        root["drawings"].push_back(std::move(item));
    }
    root["keyframes"] = nlohmann::json::array();
    for (const auto& keyframe : composition.keyframes) {
        root["keyframes"].push_back(
            {{"time", keyframe.time},
             {"drawing", keyframe.drawing},
             {"transform", transformToJson(keyframe.transform)},
             {"scanHz", keyframe.scanHz},
             {"gain", keyframe.gain},
             {"easing", scopeEasingName(keyframe.easing)},
             {"transition", scopeTransitionName(keyframe.transition)}});
    }
    return root;
}

bool scopeCompositionFromJson(const nlohmann::json& json,
                              ScopeComposition& composition,
                              std::string* error) {
    if (error) error->clear();
    try {
        if (!json.is_object() ||
            json.value("format", std::string{}) != "vwave-scope") {
            setError(error, "not a VWaveEditor scope composition");
            return false;
        }
        const int version = json.value("version", 0);
        if (version == 1) {
            ScopeComposition loaded;
            loaded.name = json.value("name", std::string{"Scope"});
            loaded.duration = 2.0;
            ScopePath path;
            if (!pathFromJson(json, path, error)) return false;
            ScopeDrawing drawing;
            drawing.id = "path";
            drawing.name = path.name;
            drawing.strokes.push_back(std::move(path));
            loaded.drawings.push_back(std::move(drawing));
            ScopeKeyframe keyframe;
            keyframe.time = 0.0;
            keyframe.drawing = "path";
            loaded.keyframes.push_back(std::move(keyframe));
            if (!loaded.validate(error)) return false;
            composition = std::move(loaded);
            return true;
        }
        if (version != 2) {
            setError(error, "unsupported scope composition version");
            return false;
        }
        ScopeComposition loaded;
        loaded.name = json.value("name", std::string{"Scope composition"});
        loaded.duration = json.value("duration", 2.0);
        if (!json.contains("drawings") || !json["drawings"].is_array() ||
            !json.contains("keyframes") || !json["keyframes"].is_array()) {
            setError(error, "scope composition is missing drawings or keyframes");
            return false;
        }
        for (const auto& item : json["drawings"]) {
            if (!item.is_object() || !item.contains("strokes") ||
                !item["strokes"].is_array()) {
                setError(error, "invalid scope drawing");
                return false;
            }
            ScopeDrawing drawing;
            drawing.id = item.value("id", std::string{});
            drawing.name = item.value("name", drawing.id);
            for (const auto& stroke : item["strokes"]) {
                ScopePath path;
                if (!pathFromJson(stroke, path, error)) return false;
                drawing.strokes.push_back(std::move(path));
            }
            loaded.drawings.push_back(std::move(drawing));
        }
        for (const auto& item : json["keyframes"]) {
            if (!item.is_object()) {
                setError(error, "invalid scope keyframe");
                return false;
            }
            ScopeKeyframe keyframe;
            keyframe.time = item.value("time", 0.0);
            keyframe.drawing = item.value("drawing", std::string{});
            keyframe.gain = item.value("gain", 1.0);
            keyframe.scanHz = item.value("scanHz", 0.0);
            if (item.contains("transform") &&
                !transformFromJson(item["transform"], keyframe.transform,
                                   error))
                return false;
            const std::string easing =
                item.value("easing", std::string{"linear"});
            if (!scopeEasingFromName(easing, keyframe.easing)) {
                setError(error, "invalid scope keyframe easing");
                return false;
            }
            const std::string transition =
                item.value("transition", std::string{"hold"});
            if (!scopeTransitionFromName(transition, keyframe.transition)) {
                setError(error, "invalid scope keyframe transition");
                return false;
            }
            loaded.keyframes.push_back(std::move(keyframe));
        }
        if (!loaded.validate(error)) return false;
        composition = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot parse scope composition: ") +
                            e.what());
        return false;
    }
}

bool saveScopeComposition(const ScopeComposition& composition,
                          const std::string& file, std::string* error) {
    if (error) error->clear();
    if (!composition.validate(error)) return false;
    try {
        std::ofstream out(file, std::ios::binary);
        if (!out) {
            setError(error, "cannot create scope composition file");
            return false;
        }
        out << scopeCompositionToJson(composition).dump(2) << '\n';
        if (!out.good()) {
            setError(error, "cannot write scope composition file");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        setError(error, std::string("cannot save scope composition: ") +
                            e.what());
        return false;
    }
}

bool loadScopeComposition(const std::string& file,
                          ScopeComposition& composition,
                          std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            setError(error, "cannot open scope composition file");
            return false;
        }
        nlohmann::json root;
        in >> root;
        return scopeCompositionFromJson(root, composition, error);
    } catch (const std::exception& e) {
        setError(error, std::string("cannot load scope composition: ") +
                            e.what());
        return false;
    }
}

} // namespace vwave
