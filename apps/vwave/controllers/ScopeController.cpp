#include "controllers/ScopeController.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "vwave/Oscilloscope.h"
#include "vwave/ScopeSvg.h"
#include "vwave/ScopeTrace.h"
#include "vwave/Recipe.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vwave::app {

ScopeController::ScopeController(vwave::JobSystem& jobs,
                                 UiDispatcher& dispatcher,
                                 vwave::Timeline& timeline,
                                 AudioPlayer& player, StatusSink status,
                                 RenderSink rendered)
    : m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_timeline(timeline),
      m_player(player),
      m_status(std::move(status)),
      m_rendered(std::move(rendered)) {}

void ScopeController::generate(const ScopeShapeParams& params) {
    if (m_shape <= 0) return;
    m_pointEditing = false;
    m_path = makeScopeShape(static_cast<vwave::ScopeShape>(m_shape),
                            params.pointCount, params.size, params.a,
                            params.b, params.phase, params.turns);
}

void ScopeController::cycleShape(const ScopeShapeParams& params) {
    m_shape = (m_shape + 1) %
              static_cast<int>(sizeof(ui::kScopeShapeNames) /
                               sizeof(ui::kScopeShapeNames[0]));
    if (m_shape == 0) {
        startPolyline();
        m_status("Polyline mode: click the scope to add points");
    } else {
        generate(params);
        m_status("Scope shape: " +
                 std::string(ui::kScopeShapeNames[m_shape]));
    }
}

void ScopeController::cycleDirection() {
    m_direction = (m_direction + 1) % 3;
}

void ScopeController::cycleOversampling() {
    m_oversampling = (m_oversampling + 1) % 4;
}

void ScopeController::setPath(vwave::ScopePath path) {
    m_shape = 0;
    m_pointEditing = false;
    m_path = std::move(path);
}

void ScopeController::startPolyline() {
    m_shape = 0;
    m_pointEditing = true;
    m_path = {};
    m_path.name = "Path";
}

void ScopeController::importImage(const std::string& path) {
    vwave::ImageData image;
    std::string error;
    if (!vwave::loadImageFile(path, image, &error)) {
        m_status(error.empty() ? "Image load failed" : error);
        return;
    }
    const vwave::ScopeTraceResult traced =
        vwave::traceImageToScope(image, {}, &error);
    if (!traced.drawing.valid()) {
        m_status(error.empty() ? "Image trace failed" : error);
        return;
    }
    vwave::ScopePath flattened = traced.drawing.flatten();
    flattened.name = traced.drawing.name;
    const size_t strokes = traced.drawing.strokes.size();
    setPath(std::move(flattened));
    m_status("Imported image: " + std::to_string(strokes) + " strokes, " +
             std::to_string(m_path.points.size()) + " points");
}

void ScopeController::importSvg(const std::string& path) {
    vwave::ScopeDrawing drawing;
    std::string error;
    if (!vwave::loadScopeSvg(path, drawing, {}, &error)) {
        m_status(error.empty() ? "SVG import failed" : error);
        return;
    }
    const size_t strokes = drawing.strokes.size();
    vwave::ScopePath flattened = drawing.flatten();
    flattened.name = drawing.name;
    setPath(std::move(flattened));
    m_status("Imported SVG: " + std::to_string(strokes) + " strokes, " +
             std::to_string(m_path.points.size()) + " points");
}

bool ScopeController::load(const std::string& path) {
    vwave::ScopeComposition loaded;
    std::string error;
    if (!vwave::loadScopeComposition(path, loaded, &error)) {
        vwave::ScopePath legacy;
        if (!vwave::loadScopePath(path, legacy, &error)) {
            m_status(error.empty() ? "Scope path load failed" : error);
            return false;
        }
        m_path = std::move(legacy);
        m_keyframes.clear();
        m_selectedKeyframe = -1;
        m_shape = 0;
        m_pointEditing = false;
        m_status("Loaded scope path " + baseName(path));
        return true;
    }
    m_duration = loaded.duration;
    m_keyframes = loaded.keyframes;
    m_selectedKeyframe = m_keyframes.empty() ? -1 : 0;
    for (auto& keyframe : m_keyframes) keyframe.drawing = "main";
    vwave::ScopeDrawing merged;
    merged.id = "main";
    for (const auto& drawing : loaded.drawings) {
        merged.strokes.insert(merged.strokes.end(), drawing.strokes.begin(),
                              drawing.strokes.end());
    }
    m_path = merged.flatten();
    m_shape = 0;
    m_pointEditing = false;
    m_status("Loaded scope composition " + baseName(path));
    return true;
}

void ScopeController::save(const std::string& path) const {
    if (!m_path.valid()) {
        m_status("Create a scope path first");
        return;
    }
    const std::string target = withExtension(path, ".vscope");
    std::string error;
    const bool success =
        !m_keyframes.empty()
            ? vwave::saveScopeComposition(composition(), target, &error)
            : vwave::saveScopePath(m_path, target, &error);
    if (!success) {
        m_status(error.empty() ? "Scope path save failed" : error);
        return;
    }
    if (!m_keyframes.empty())
        m_status("Saved scope composition " + baseName(target));
    else
        m_status("Saved scope path " + baseName(target));
}

void ScopeController::render(int64_t playhead,
                             const ScopeRenderParams& params) {
    if (!m_path.valid()) {
        m_status("Create or load a scope path first");
        return;
    }
    vwave::ScopeRenderOptions options;
    options.sampleRate =
        m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000;
    options.duration = params.duration;
    setDuration(params.duration);
    options.scanHz = params.scanHz;
    options.gain = params.gain;
    options.smoothing = params.smoothing;
    options.bandLimited = m_bandLimited;
    options.bandLimitHz = 16000.0;
    options.oversampling = ui::kScopeOversampling[m_oversampling];
    options.direction = static_cast<vwave::ScopeDirection>(m_direction);
    options.normalize = m_normalize;
    options.peak = 0.9;

    const vwave::ScopePath path = m_path;
    const vwave::ScopeComposition composition = this->composition();
    const bool animate = !m_keyframes.empty();
    const int64_t start = ui::clampFrame(playhead, m_timeline.lengthFrames());
    const std::string name =
        path.name.empty() ? std::string("Scope") : path.name;
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    m_job = m_jobs.submit(
        [this, request, path, composition, animate, options, start, name](
            vwave::JobControl& control) mutable {
            std::string error;
            vwave::Audio rendered;
            try {
                rendered = animate
                               ? vwave::renderScopeComposition(
                                     composition, options, &error)
                               : vwave::renderScopePath(path, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, rendered = std::move(rendered),
                 error = std::move(error), path, composition, animate,
                 options, start, name]() mutable {
                    if (request != m_request) return;
                    if (rendered.empty()) {
                        m_status(error.empty() ? "Scope render failed" : error);
                        return;
                    }
                    m_player.stop();
                    std::string addError;
                    if (!m_timeline.addAudioTrack(name, rendered, start,
                                                  &addError)) {
                        m_status("Cannot add scope track: " + addError);
                        return;
                    }
                    const int trackIndex = m_timeline.trackCount() - 1;
                    m_timeline.track(trackIndex).recipe =
                        animate ? vwave::makeScopeCompositionRecipe(
                                      composition, options)
                                : vwave::makeScopeTrackRecipe(path, options);
                    m_timeline.setActiveTrack(trackIndex);
                    m_rendered(start);
                    m_status("Rendered scope path to track (" +
                             ui::formatSeconds(rendered.duration()) + ")");
                });
        });
    m_status("Rendering scope in background...");
}

vwave::ScopeComposition ScopeController::composition() const {
    vwave::ScopeComposition result;
    result.name = m_path.name.empty() ? "Scope path" : m_path.name;
    result.duration = std::max(0.01, m_duration);
    vwave::ScopeDrawing drawing;
    drawing.id = "main";
    drawing.name = result.name;
    if (m_path.valid()) drawing.strokes.push_back(m_path);
    result.drawings.push_back(std::move(drawing));
    result.keyframes = m_keyframes;
    return result;
}

vwave::ScopeTransform ScopeController::selectedTransform() const {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return {};
    return m_keyframes[static_cast<size_t>(m_selectedKeyframe)].transform;
}

vwave::ScopeTransform ScopeController::transformAt(double time) const {
    if (m_keyframes.empty()) return {};
    if (time <= m_keyframes.front().time)
        return m_keyframes.front().transform;
    if (time >= m_keyframes.back().time)
        return m_keyframes.back().transform;
    auto upper = std::upper_bound(
        m_keyframes.begin(), m_keyframes.end(), time,
        [](double value, const vwave::ScopeKeyframe& keyframe) {
            return value < keyframe.time;
        });
    const vwave::ScopeKeyframe& first = *(upper - 1);
    const vwave::ScopeKeyframe& second = *upper;
    const double span = second.time - first.time;
    const double progress =
        span > 1e-12 ? std::clamp((time - first.time) / span, 0.0, 1.0)
                     : 1.0;
    return vwave::interpolateScopeTransform(
        first.transform, second.transform,
        vwave::scopeEasingValue(first.easing, progress));
}

vwave::ScopeEasing ScopeController::selectedEasing() const {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return vwave::ScopeEasing::Linear;
    return m_keyframes[static_cast<size_t>(m_selectedKeyframe)].easing;
}

vwave::ScopeTransition ScopeController::selectedTransition() const {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return vwave::ScopeTransition::Hold;
    return m_keyframes[static_cast<size_t>(m_selectedKeyframe)].transition;
}

double ScopeController::selectedKeyframeTime() const {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return 0.0;
    return m_keyframes[static_cast<size_t>(m_selectedKeyframe)].time;
}

void ScopeController::setDuration(double duration) {
    if (std::isfinite(duration) && duration > 0.0) m_duration = duration;
}

void ScopeController::setSelectedKeyframe(int index) {
    if (m_keyframes.empty()) {
        m_selectedKeyframe = -1;
        return;
    }
    if (index < 0) {
        m_selectedKeyframe = -1;
        return;
    }
    m_selectedKeyframe =
        std::clamp(index, 0, static_cast<int>(m_keyframes.size()) - 1);
}

void ScopeController::addKeyframe(double time) {
    if (!m_path.valid()) {
        m_status("Create or import a scope path first");
        return;
    }
    time = std::max(0.0, time);
    for (size_t i = 0; i < m_keyframes.size(); ++i) {
        if (std::abs(m_keyframes[i].time - time) < 1e-9) {
            m_selectedKeyframe = static_cast<int>(i);
            return;
        }
    }
    vwave::ScopeKeyframe keyframe;
    keyframe.time = time;
    keyframe.drawing = "main";
    if (m_keyframes.empty()) {
        m_keyframes.push_back(keyframe);
        m_selectedKeyframe = 0;
        m_status("Keyframe 1 added");
        return;
    }
    if (m_selectedKeyframe >= 0 &&
               m_selectedKeyframe < static_cast<int>(m_keyframes.size())) {
        keyframe.transform =
            m_keyframes[static_cast<size_t>(m_selectedKeyframe)].transform;
    }
    m_keyframes.push_back(std::move(keyframe));
    std::stable_sort(m_keyframes.begin(), m_keyframes.end(),
                     [](const vwave::ScopeKeyframe& a,
                        const vwave::ScopeKeyframe& b) {
                         return a.time < b.time;
                     });
    m_selectedKeyframe = -1;
    for (size_t i = 0; i < m_keyframes.size(); ++i) {
        if (std::abs(m_keyframes[i].time - time) < 1e-9) {
            m_selectedKeyframe = static_cast<int>(i);
            break;
        }
    }
    m_status("Keyframe " + std::to_string(m_selectedKeyframe + 1) + " added");
}

void ScopeController::deleteSelectedKeyframe() {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size())) {
        m_status("Select a keyframe first");
        return;
    }
    m_keyframes.erase(m_keyframes.begin() + m_selectedKeyframe);
    if (m_keyframes.empty()) {
        m_selectedKeyframe = -1;
    } else {
        m_selectedKeyframe =
            std::clamp(m_selectedKeyframe, 0,
                       static_cast<int>(m_keyframes.size()) - 1);
    }
    m_status("Keyframe deleted");
}

void ScopeController::moveSelectedKeyframe(double time) {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size())) {
        m_status("Select a keyframe first");
        return;
    }
    m_keyframes[static_cast<size_t>(m_selectedKeyframe)].time =
        std::max(0.0, time);
    std::stable_sort(m_keyframes.begin(), m_keyframes.end(),
                     [](const vwave::ScopeKeyframe& a,
                        const vwave::ScopeKeyframe& b) {
                         return a.time < b.time;
                     });
    m_selectedKeyframe = -1;
    for (size_t i = 0; i < m_keyframes.size(); ++i) {
        if (std::abs(m_keyframes[i].time - std::max(0.0, time)) < 1e-9) {
            m_selectedKeyframe = static_cast<int>(i);
            break;
        }
    }
    m_status("Keyframe moved");
}

void ScopeController::setSelectedTransform(
    const vwave::ScopeTransform& transform) {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return;
    m_keyframes[static_cast<size_t>(m_selectedKeyframe)].transform = transform;
}

void ScopeController::cycleSelectedEasing() {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return;
    auto& easing =
        m_keyframes[static_cast<size_t>(m_selectedKeyframe)].easing;
    easing = static_cast<vwave::ScopeEasing>(
        (static_cast<int>(easing) + 1) % 6);
}

void ScopeController::cycleSelectedTransition() {
    if (m_selectedKeyframe < 0 ||
        m_selectedKeyframe >= static_cast<int>(m_keyframes.size()))
        return;
    auto& transition =
        m_keyframes[static_cast<size_t>(m_selectedKeyframe)].transition;
    transition = transition == vwave::ScopeTransition::Hold
                     ? vwave::ScopeTransition::Crossfade
                     : vwave::ScopeTransition::Hold;
}

void ScopeController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
