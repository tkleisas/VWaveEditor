#include "vwave/Session.h"

#include "CommandHelpers.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Recipe.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace vwave::session;

namespace vwave {

nlohmann::json Session::executeImageScopeCommands(const std::string& command,
                                                 const nlohmann::json& params,
                                                 std::string* error) {
        if (command == "oscilloscope_render") {
            const std::string out = requiredString(params, "out", error);
            if (out.empty()) return nullptr;
            ScopePath path;
            std::string shapeName = "polyline";
            if (params.contains("path_file")) {
                const std::string file = requiredString(
                    params, "path_file", error);
                if (file.empty() ||
                    !loadScopePath(file, path, error))
                    return nullptr;
            } else if (params.contains("points") &&
                       params["points"].is_array()) {
                if (params["points"].size() < 2) {
                    setError(error, "scope points must be an array");
                    return nullptr;
                }
                path.name = params.value("name", std::string{"Path"});
                path.closed = optionalBool(params, "closed", false);
                for (const auto& point : params["points"]) {
                    if (!point.is_array() || point.size() != 2 ||
                        !point[0].is_number() || !point[1].is_number()) {
                        setError(error, "scope points must be [x, y] pairs");
                        return nullptr;
                    }
                    const double x = point[0].get<double>();
                    const double y = point[1].get<double>();
                    if (!std::isfinite(x) || !std::isfinite(y)) {
                        setError(error, "scope point is not finite");
                        return nullptr;
                    }
                    path.points.push_back({x, y});
                }
            } else {
                ScopeShape shape = ScopeShape::Circle;
                shapeName = params.value("shape", std::string{"circle"});
                if (!scopeShapeFromName(shapeName, shape)) {
                    setError(error, "unknown scope shape: " + shapeName);
                    return nullptr;
                }
                path = makeScopeShape(
                    shape, optionalInt(params, "point_count", 256),
                    optionalDouble(params, "size", 0.8),
                    optionalDouble(params, "a", 3.0),
                    optionalDouble(params, "b", 2.0),
                    optionalDouble(params, "phase", 0.0),
                    optionalInt(params, "turns", 4),
                    params.value("name", std::string{}));
            }
            if (!path.valid()) {
                setError(error, "scope path is empty");
                return nullptr;
            }
            ScopeRenderOptions options;
            options.sampleRate = optionalInt(
                params, "sample_rate",
                m_timeline.sampleRate() > 0 ? m_timeline.sampleRate()
                                            : 48000);
            options.duration = optionalDouble(params, "duration", 2.0);
            options.scanHz = optionalDouble(params, "scan_hz", 120.0);
            options.gain = optionalDouble(params, "gain", 0.8);
            options.smoothing = optionalDouble(params, "smoothing", 0.0);
            options.bandLimited =
                optionalBool(params, "band_limited", true);
            options.bandLimitHz =
                optionalDouble(params, "band_limit_hz", 16000.0);
            options.oversampling = optionalInt(params, "oversampling", 4);
            options.normalize = optionalBool(params, "normalize", true);
            options.peak = optionalDouble(params, "peak", 0.9);
            if (params.contains("direction")) {
                if (!params["direction"].is_string()) {
                    setError(error, "direction must be a string");
                    return nullptr;
                }
                const std::string directionName =
                    params["direction"].get<std::string>();
                if (!scopeDirectionFromName(directionName,
                                            options.direction)) {
                    setError(error,
                             "unknown scope direction: " + directionName);
                    return nullptr;
                }
            }
            const Audio rendered = renderScopePath(path, options, error);
            if (rendered.empty()) return nullptr;
            AudioExportOptions exportOptions =
                exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!writeAudio(out, rendered, exportOptions, error))
                return nullptr;
            if (params.contains("save_path")) {
                const std::string savePath = requiredString(
                    params, "save_path", error);
                if (savePath.empty() ||
                    !saveScopePath(path, savePath, error))
                    return nullptr;
            }
            if (optionalBool(params, "replace", false)) {
                int trackIndex = -1;
                if (m_timeline.empty()) {
                    if (!m_timeline.addAudioTrack(
                            path.name.empty() ? "Scope" : path.name,
                            rendered, 0, error))
                        return nullptr;
                    trackIndex = 0;
                } else {
                    const int index = m_timeline.activeTrack();
                    if (!m_timeline.setTrackAudio(
                            index, rendered,
                            m_timeline.track(index).startFrame, error))
                        return nullptr;
                    trackIndex = index;
                }
                if (trackIndex >= 0)
                    m_timeline.track(trackIndex).recipe =
                        makeScopeTrackRecipe(path, options);
            }
            return {{"ok", true},
                    {"path", out},
                    {"name", path.name},
                    {"shape", shapeName},
                    {"points", path.points.size()},
                    {"closed", path.closed},
                    {"sample_rate", rendered.sampleRate()},
                    {"channels", rendered.numChannels()},
                    {"frames", rendered.numFrames()},
                    {"duration", rendered.duration()},
                    {"scan_hz", options.scanHz},
                    {"band_limited", options.bandLimited},
                    {"oversampling", options.oversampling},
                    {"direction",
                     scopeDirectionName(options.direction)}};
        }

        if (command == "image_spectrogram") {
            const std::string imagePath = requiredString(params, "path", error);
            if (imagePath.empty()) return nullptr;
            const std::string out = requiredString(params, "out", error);
            if (out.empty()) return nullptr;
            ImageData image;
            if (!loadImageFile(imagePath, image, error)) return nullptr;
            ImageSpectrogramOptions options;
            options.sampleRate = optionalInt(
                params, "sample_rate",
                m_timeline.sampleRate() > 0 ? m_timeline.sampleRate()
                                            : 48000);
            options.fftSize = optionalInt(params, "fft_size", 2048);
            options.hopSize = optionalInt(params, "hop_size", 512);
            options.duration = optionalDouble(params, "duration", 0.0);
            options.minHz = optionalDouble(params, "min_hz", 20.0);
            options.maxHz = optionalDouble(params, "max_hz", 20000.0);
            options.loops = optionalInt(params, "loops", 1);
            options.cropLeft = optionalDouble(params, "crop_left", 0.0);
            options.cropRight = optionalDouble(params, "crop_right", 1.0);
            options.cropTop = optionalDouble(params, "crop_top", 0.0);
            options.cropBottom =
                optionalDouble(params, "crop_bottom", 1.0);
            options.flipHorizontal =
                optionalBool(params, "flip_horizontal", false);
            options.flipVertical =
                optionalBool(params, "flip_vertical", false);
            options.invert = optionalBool(params, "invert", false);
            options.gamma = optionalDouble(params, "gamma", 1.0);
            options.contrast = optionalDouble(params, "contrast", 1.0);
            options.threshold =
                optionalDouble(params, "threshold", 0.0);
            options.useAlpha = optionalBool(params, "use_alpha", true);
            options.iterations = optionalInt(params, "iterations", 24);
            options.momentum = optionalDouble(params, "momentum", 0.99);
            options.seed = static_cast<uint32_t>(
                optionalInt(params, "seed", 1));
            options.channels = optionalInt(params, "channels", 1);
            options.normalize = optionalBool(params, "normalize", true);
            options.peak = optionalDouble(params, "peak", 0.9);
            if (params.contains("frequency_scale")) {
                if (!params["frequency_scale"].is_string()) {
                    setError(error, "frequency_scale must be a string");
                    return nullptr;
                }
                const std::string name =
                    params["frequency_scale"].get<std::string>();
                if (!imageFrequencyScaleFromName(name,
                                                 options.frequencyScale)) {
                    setError(error,
                             "unknown image frequency scale: " + name);
                    return nullptr;
                }
            }
            if (params.contains("time_mapping")) {
                if (!params["time_mapping"].is_string()) {
                    setError(error, "time_mapping must be a string");
                    return nullptr;
                }
                const std::string name =
                    params["time_mapping"].get<std::string>();
                if (!imageTimeMappingFromName(name, options.timeMapping)) {
                    setError(error, "unknown image time mapping: " + name);
                    return nullptr;
                }
            }
            const std::string phaseName =
                params.value("phase_mode", std::string{"fast_griffin_lim"});
            if (!imagePhaseModeFromName(phaseName, options.phaseMode)) {
                setError(error, "unknown image phase mode: " + phaseName);
                return nullptr;
            }
            const ImageSpectrogramResult generated =
                imageToSpectrogram(image, options, error);
            if (generated.audio.empty()) return nullptr;
            AudioExportOptions exportOptions =
                exportOptionsFrom(params, error);
            if (error && !error->empty()) return nullptr;
            if (!writeAudio(out, generated.audio, exportOptions, error))
                return nullptr;
            if (optionalBool(params, "replace", false)) {
                int trackIndex = -1;
                if (m_timeline.empty()) {
                    if (!m_timeline.addAudioTrack("Image", generated.audio, 0,
                                                  error))
                        return nullptr;
                    trackIndex = 0;
                } else {
                    const int index = m_timeline.activeTrack();
                    if (!m_timeline.setTrackAudio(
                            index, generated.audio,
                            m_timeline.track(index).startFrame, error))
                        return nullptr;
                    trackIndex = index;
                }
                if (trackIndex >= 0)
                    m_timeline.track(trackIndex).recipe =
                        makeImageTrackRecipe(imagePath, options);
            }
            return {{"ok", true},
                    {"path", out},
                    {"image_width", generated.imageWidth},
                    {"image_height", generated.imageHeight},
                    {"sample_rate", generated.audio.sampleRate()},
                    {"channels", generated.audio.numChannels()},
                    {"frames", generated.audio.numFrames()},
                    {"duration", generated.duration},
                    {"fft_size", generated.fftSize},
                    {"hop_size", generated.hopSize},
                    {"frame_count", generated.frameCount},
                    {"bin_count", generated.binCount},
                    {"phase_mode", imagePhaseModeName(options.phaseMode)},
                    {"frequency_scale",
                     imageFrequencyScaleName(options.frequencyScale)},
                    {"time_mapping",
                     imageTimeMappingName(options.timeMapping)}};
        }

    setError(error, "unknown command: " + command);
    return nullptr;
}

} // namespace vwave
