#pragma once

#include "vwave/Oscilloscope.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace vwave {

struct ScopeDrawing {
    std::string id;
    std::string name;
    std::vector<ScopePath> strokes;

    bool valid() const;
    ScopePath flatten() const;
};

enum class ScopeEasing {
    Hold,
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Smoothstep,
};

enum class ScopeTransition {
    Hold,
    Crossfade,
};

const char* scopeEasingName(ScopeEasing easing);
bool scopeEasingFromName(const std::string& name, ScopeEasing& easing);
const char* scopeTransitionName(ScopeTransition transition);
bool scopeTransitionFromName(const std::string& name,
                             ScopeTransition& transition);

double scopeEasingValue(ScopeEasing easing, double progress);

struct ScopeTransform {
    double translateX = 0.0;
    double translateY = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;
    double pivotX = 0.0;
    double pivotY = 0.0;
};

ScopePoint applyScopeTransform(const ScopeTransform& transform,
                               ScopePoint point);
ScopeTransform interpolateScopeTransform(const ScopeTransform& a,
                                         const ScopeTransform& b,
                                         double progress);

struct ScopeKeyframe {
    double time = 0.0;
    std::string drawing;
    ScopeTransform transform;
    double gain = 1.0;
    ScopeEasing easing = ScopeEasing::Linear;
    ScopeTransition transition = ScopeTransition::Hold;
};

struct ScopeComposition {
    std::string name = "Scope composition";
    double duration = 2.0;
    std::vector<ScopeDrawing> drawings;
    std::vector<ScopeKeyframe> keyframes;

    bool valid() const;
    bool validate(std::string* error = nullptr) const;
    const ScopeDrawing* findDrawing(const std::string& id) const;
};

Audio renderScopeComposition(const ScopeComposition& composition,
                             const ScopeRenderOptions& options,
                             std::string* error = nullptr);

nlohmann::json scopeCompositionToJson(const ScopeComposition& composition);
bool scopeCompositionFromJson(const nlohmann::json& json,
                              ScopeComposition& composition,
                              std::string* error = nullptr);
bool saveScopeComposition(const ScopeComposition& composition,
                          const std::string& file,
                          std::string* error = nullptr);
bool loadScopeComposition(const std::string& file,
                          ScopeComposition& composition,
                          std::string* error = nullptr);

} // namespace vwave
