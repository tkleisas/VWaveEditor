#include "controllers/ScopeController.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Recipe.h"

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
    m_path = std::move(path);
}

void ScopeController::startPolyline() {
    m_shape = 0;
    m_path = {};
    m_path.name = "Path";
}

bool ScopeController::load(const std::string& path) {
    vwave::ScopePath loaded;
    std::string error;
    if (!vwave::loadScopePath(path, loaded, &error)) {
        m_status(error.empty() ? "Scope path load failed" : error);
        return false;
    }
    m_path = std::move(loaded);
    m_shape = 0;
    m_status("Loaded scope path " + baseName(path));
    return true;
}

void ScopeController::save(const std::string& path) const {
    if (!m_path.valid()) {
        m_status("Create a scope path first");
        return;
    }
    const std::string target = withExtension(path, ".vscope");
    std::string error;
    if (!vwave::saveScopePath(m_path, target, &error)) {
        m_status(error.empty() ? "Scope path save failed" : error);
        return;
    }
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
    const int64_t start = ui::clampFrame(playhead, m_timeline.lengthFrames());
    const std::string name =
        path.name.empty() ? std::string("Scope") : path.name;
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    m_job = m_jobs.submit(
        [this, request, path, options, start, name](
            vwave::JobControl& control) mutable {
            std::string error;
            vwave::Audio rendered;
            try {
                rendered = vwave::renderScopePath(path, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, rendered = std::move(rendered),
                 error = std::move(error), path, options, start,
                 name]() mutable {
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
                        vwave::makeScopeTrackRecipe(path, options);
                    m_timeline.setActiveTrack(trackIndex);
                    m_rendered(start);
                    m_status("Rendered scope path to track (" +
                             ui::formatSeconds(rendered.duration()) + ")");
                });
        });
    m_status("Rendering scope in background...");
}

void ScopeController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
