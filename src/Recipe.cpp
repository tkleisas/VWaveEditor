#include "vwave/Recipe.h"

#include <algorithm>
#include <cmath>

namespace vwave {

namespace {

nlohmann::json envelopeJson(const Adsr& envelope) {
    return {{"attack", envelope.attack},
            {"decay", envelope.decay},
            {"sustain", envelope.sustain},
            {"release", envelope.release}};
}

Adsr envelopeFromJson(const nlohmann::json& json) {
    Adsr envelope;
    envelope.attack = json.value("attack", envelope.attack);
    envelope.decay = json.value("decay", envelope.decay);
    envelope.sustain = json.value("sustain", envelope.sustain);
    envelope.release = json.value("release", envelope.release);
    envelope.attack = std::max(0.0, envelope.attack);
    envelope.decay = std::max(0.0, envelope.decay);
    envelope.sustain = std::clamp(envelope.sustain, 0.0, 1.0);
    envelope.release = std::max(0.0, envelope.release);
    return envelope;
}

bool parseImageOptions(const nlohmann::json& json,
                       ImageSpectrogramOptions& options,
                       std::string* error) {
    options.sampleRate = json.value("sample_rate", options.sampleRate);
    options.fftSize = json.value("fft_size", options.fftSize);
    options.hopSize = json.value("hop_size", options.hopSize);
    options.duration = json.value("duration", options.duration);
    options.minHz = json.value("min_hz", options.minHz);
    options.maxHz = json.value("max_hz", options.maxHz);
    options.loops = json.value("loops", options.loops);
    options.cropLeft = json.value("crop_left", options.cropLeft);
    options.cropRight = json.value("crop_right", options.cropRight);
    options.cropTop = json.value("crop_top", options.cropTop);
    options.cropBottom = json.value("crop_bottom", options.cropBottom);
    options.flipHorizontal =
        json.value("flip_horizontal", options.flipHorizontal);
    options.flipVertical =
        json.value("flip_vertical", options.flipVertical);
    options.invert = json.value("invert", options.invert);
    options.gamma = json.value("gamma", options.gamma);
    options.contrast = json.value("contrast", options.contrast);
    options.threshold = json.value("threshold", options.threshold);
    options.useAlpha = json.value("use_alpha", options.useAlpha);
    options.iterations = json.value("iterations", options.iterations);
    options.momentum = json.value("momentum", options.momentum);
    options.seed = static_cast<uint32_t>(json.value("seed", options.seed));
    options.channels = json.value("channels", options.channels);
    options.normalize = json.value("normalize", options.normalize);
    options.peak = json.value("peak", options.peak);
    const std::string scale =
        json.value("frequency_scale",
                   std::string(imageFrequencyScaleName(options.frequencyScale)));
    if (!imageFrequencyScaleFromName(scale, options.frequencyScale)) {
        if (error) *error = "invalid image frequency scale in recipe";
        return false;
    }
    const std::string mapping =
        json.value("time_mapping",
                   std::string(imageTimeMappingName(options.timeMapping)));
    if (!imageTimeMappingFromName(mapping, options.timeMapping)) {
        if (error) *error = "invalid image time mapping in recipe";
        return false;
    }
    const std::string phase =
        json.value("phase_mode",
                   std::string(imagePhaseModeName(options.phaseMode)));
    if (!imagePhaseModeFromName(phase, options.phaseMode)) {
        if (error) *error = "invalid image phase mode in recipe";
        return false;
    }
    return true;
}

nlohmann::json imageOptionsJson(const ImageSpectrogramOptions& options) {
    return {{"sample_rate", options.sampleRate},
            {"fft_size", options.fftSize},
            {"hop_size", options.hopSize},
            {"duration", options.duration},
            {"min_hz", options.minHz},
            {"max_hz", options.maxHz},
            {"frequency_scale",
             imageFrequencyScaleName(options.frequencyScale)},
            {"time_mapping", imageTimeMappingName(options.timeMapping)},
            {"loops", options.loops},
            {"crop_left", options.cropLeft},
            {"crop_right", options.cropRight},
            {"crop_top", options.cropTop},
            {"crop_bottom", options.cropBottom},
            {"flip_horizontal", options.flipHorizontal},
            {"flip_vertical", options.flipVertical},
            {"invert", options.invert},
            {"gamma", options.gamma},
            {"contrast", options.contrast},
            {"threshold", options.threshold},
            {"use_alpha", options.useAlpha},
            {"phase_mode", imagePhaseModeName(options.phaseMode)},
            {"iterations", options.iterations},
            {"momentum", options.momentum},
            {"seed", options.seed},
            {"channels", options.channels},
            {"normalize", options.normalize},
            {"peak", options.peak}};
}

nlohmann::json scopeOptionsJson(const ScopeRenderOptions& options) {
    return {{"sample_rate", options.sampleRate},
            {"duration", options.duration},
            {"scan_hz", options.scanHz},
            {"gain", options.gain},
            {"smoothing", options.smoothing},
            {"band_limited", options.bandLimited},
            {"band_limit_hz", options.bandLimitHz},
            {"oversampling", options.oversampling},
            {"direction", scopeDirectionName(options.direction)},
            {"normalize", options.normalize},
            {"peak", options.peak}};
}

bool parseScopeOptions(const nlohmann::json& json, ScopeRenderOptions& options,
                       std::string* error) {
    options.sampleRate = json.value("sample_rate", options.sampleRate);
    options.duration = json.value("duration", options.duration);
    options.scanHz = json.value("scan_hz", options.scanHz);
    options.gain = json.value("gain", options.gain);
    options.smoothing = json.value("smoothing", options.smoothing);
    options.bandLimited = json.value("band_limited", options.bandLimited);
    options.bandLimitHz = json.value("band_limit_hz", options.bandLimitHz);
    options.oversampling = json.value("oversampling", options.oversampling);
    options.normalize = json.value("normalize", options.normalize);
    options.peak = json.value("peak", options.peak);
    const std::string direction =
        json.value("direction",
                   std::string(scopeDirectionName(options.direction)));
    if (!scopeDirectionFromName(direction, options.direction)) {
        if (error) *error = "invalid scope direction in recipe";
        return false;
    }
    return true;
}

nlohmann::json scopePathJson(const ScopePath& path) {
    nlohmann::json json;
    json["name"] = path.name;
    json["closed"] = path.closed;
    json["points"] = nlohmann::json::array();
    for (const auto& point : path.points)
        json["points"].push_back({point.x, point.y});
    return json;
}

bool parseScopePathJson(const nlohmann::json& json, ScopePath& path,
                        std::string* error) {
    if (!json.is_object() || !json.contains("points") ||
        !json["points"].is_array() || json["points"].size() < 2) {
        if (error) *error = "invalid scope path in recipe";
        return false;
    }
    ScopePath loaded;
    loaded.name = json.value("name", std::string{"Path"});
    loaded.closed = json.value("closed", true);
    for (const auto& point : json["points"]) {
        if (!point.is_array() || point.size() != 2 ||
            !point[0].is_number() || !point[1].is_number()) {
            if (error) *error = "invalid scope point in recipe";
            return false;
        }
        const double x = point[0].get<double>();
        const double y = point[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y)) {
            if (error) *error = "invalid scope point in recipe";
            return false;
        }
        loaded.points.push_back({x, y});
    }
    if (!loaded.valid()) {
        if (error) *error = "invalid scope path in recipe";
        return false;
    }
    path = std::move(loaded);
    return true;
}

nlohmann::json synthOptionsJson(const SynthNoteOptions& options) {
    nlohmann::json json;
    json["sample_rate"] = options.sampleRate;
    json["channels"] = options.channels;
    json["root_note"] = options.rootNote;
    json["morph"] = options.morph;
    json["gain"] = options.gain;
    json["unison"] = options.unison;
    json["detune_cents"] = options.detuneCents;
    json["oversampling"] = options.oversampling;
    json["use_table_root"] = options.useWavetableRootNote;
    json["blend_weights"] = options.blendWeights;
    json["envelope"] = envelopeJson(options.envelope);
    return json;
}

bool parseSynthOptions(const nlohmann::json& json, SynthNoteOptions& options,
                       std::string* error) {
    options.sampleRate = json.value("sample_rate", options.sampleRate);
    options.channels = json.value("channels", options.channels);
    options.rootNote = json.value("root_note", options.rootNote);
    options.morph = json.value("morph", options.morph);
    options.gain = json.value("gain", options.gain);
    options.unison = json.value("unison", options.unison);
    options.detuneCents = json.value("detune_cents", options.detuneCents);
    options.oversampling = json.value("oversampling", options.oversampling);
    options.useWavetableRootNote =
        json.value("use_table_root", options.useWavetableRootNote);
    if (json.contains("blend_weights")) {
        if (!json["blend_weights"].is_array()) {
            if (error) *error = "invalid blend weights in recipe";
            return false;
        }
        options.blendWeights.clear();
        for (const auto& value : json["blend_weights"]) {
            if (!value.is_number()) {
                if (error) *error = "invalid blend weights in recipe";
                return false;
            }
            options.blendWeights.push_back(value.get<double>());
        }
    }
    if (json.contains("envelope"))
        options.envelope = envelopeFromJson(json["envelope"]);
    return true;
}

} // namespace

nlohmann::json makeImageTrackRecipe(
    const std::string& imagePath, const ImageSpectrogramOptions& options) {
    return {{"type", "image_spectrogram"},
            {"version", 1},
            {"image_path", imagePath},
            {"options", imageOptionsJson(options)}};
}

nlohmann::json makeScopeTrackRecipe(const ScopePath& path,
                                    const ScopeRenderOptions& options) {
    return {{"type", "oscilloscope_render"},
            {"version", 1},
            {"scope_path", scopePathJson(path)},
            {"options", scopeOptionsJson(options)}};
}

nlohmann::json makeSynthTrackRecipe(const std::string& wavetablePath,
                                   double midiNote, double durationSeconds,
                                   const SynthNoteOptions& options) {
    return {{"type", "synth_note"},
            {"version", 1},
            {"wavetable_path", wavetablePath},
            {"midi_note", midiNote},
            {"duration", durationSeconds},
            {"options", synthOptionsJson(options)}};
}

bool renderTrackRecipe(const nlohmann::json& recipe, Audio& out,
                       std::string* error) {
    if (error) error->clear();
    out = {};
    if (!recipe.is_object()) {
        if (error) *error = "track has no recipe";
        return false;
    }
    if (recipe.value("version", 0) != 1) {
        if (error) *error = "unsupported recipe version";
        return false;
    }
    const std::string type = recipe.value("type", std::string{});
    if (type == "image_spectrogram") {
        const std::string imagePath =
            recipe.value("image_path", std::string{});
        if (imagePath.empty()) {
            if (error) *error = "image recipe has no path";
            return false;
        }
        ImageSpectrogramOptions options;
        if (recipe.contains("options") &&
            !parseImageOptions(recipe["options"], options, error))
            return false;
        ImageData image;
        if (!loadImageFile(imagePath, image, error)) return false;
        const ImageSpectrogramResult rendered =
            imageToSpectrogram(image, options, error);
        if (rendered.audio.empty()) return false;
        out = rendered.audio;
        return true;
    }
    if (type == "oscilloscope_render") {
        ScopePath path;
        if (!recipe.contains("scope_path") ||
            !parseScopePathJson(recipe["scope_path"], path, error))
            return false;
        ScopeRenderOptions options;
        if (recipe.contains("options") &&
            !parseScopeOptions(recipe["options"], options, error))
            return false;
        const Audio rendered = renderScopePath(path, options, error);
        if (rendered.empty()) return false;
        out = rendered;
        return true;
    }
    if (type == "synth_note") {
        const std::string wavetablePath =
            recipe.value("wavetable_path", std::string{});
        if (wavetablePath.empty()) {
            if (error) *error = "synth recipe has no wavetable path";
            return false;
        }
        Wavetable wavetable;
        if (!Wavetable::loadAuto(wavetablePath, wavetable, error))
            return false;
        SynthNoteOptions options;
        if (recipe.contains("options") &&
            !parseSynthOptions(recipe["options"], options, error))
            return false;
        const Audio rendered = renderWavetableNote(
            wavetable, recipe.value("midi_note", 60.0),
            recipe.value("duration", 1.0), options);
        if (rendered.empty()) {
            if (error) *error = "cannot render synth recipe";
            return false;
        }
        out = rendered;
        return true;
    }
    if (error) *error = "unsupported track recipe type: " + type;
    return false;
}

bool regenerateTrack(const Timeline& timeline, int trackIndex, Audio& out,
                     std::string* error) {
    if (error) error->clear();
    if (trackIndex < 0 || trackIndex >= timeline.trackCount()) {
        if (error) *error = "track index is out of range";
        return false;
    }
    return renderTrackRecipe(timeline.track(trackIndex).recipe, out, error);
}

} // namespace vwave
