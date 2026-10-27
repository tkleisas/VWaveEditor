#include "controllers/WavetableController.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "vwave/Wavetable.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace vwave::app {

WavetableController::WavetableController(vwave::JobSystem& jobs,
                                         UiDispatcher& dispatcher,
                                         vwave::Timeline& timeline,
                                         AudioPlayer& player,
                                         StatusSink status, RenderSink rendered)
    : m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_timeline(timeline),
      m_player(player),
      m_status(std::move(status)),
      m_rendered(std::move(rendered)) {}

void WavetableController::clear() {
    m_table = {};
    m_weights.clear();
    m_view.selectedFrame = 0;
}

void WavetableController::syncWeights() {
    if (!m_table.valid()) return;
    if (static_cast<int>(m_weights.size()) != m_table.frameCount())
        m_weights.assign(static_cast<size_t>(m_table.frameCount()), 1.0);
    m_view.clamp(m_table.frameCount());
}

void WavetableController::extract(
    const vwave::Audio& audio, const vwave::WavetableExtractOptions& options) {
    std::string error;
    m_table = vwave::extractWavetable(audio, options, &error);
    if (!m_table.valid()) {
        m_status(error.empty() ? "Wavetable extraction failed" : error);
        return;
    }
    m_weights.assign(static_cast<size_t>(m_table.frameCount()), 1.0);
    m_freeBlend = false;
    m_view.selectedFrame = 0;
    m_status("Extracted " + std::to_string(m_table.frameCount()) +
             " wavetable frames");
}

bool WavetableController::load(const std::string& path) {
    std::string error;
    vwave::Wavetable loaded;
    if (!vwave::Wavetable::loadAuto(path, loaded, &error)) {
        m_status(error.empty() ? "Wavetable load failed" : error);
        return false;
    }
    m_table = std::move(loaded);
    m_weights.assign(static_cast<size_t>(m_table.frameCount()), 1.0);
    m_freeBlend = false;
    m_view.selectedFrame = 0;
    m_status("Loaded wavetable " + baseName(path));
    return true;
}

void WavetableController::save(const std::string& path,
                               int sampleRate) const {
    if (!m_table.valid()) {
        m_status("No wavetable to save");
        return;
    }
    const std::string extension =
        std::filesystem::path(path).extension().string();
    std::string error;
    std::string target = path;
    bool success = false;
    if (extension == ".wav") {
        success = m_table.saveWav(path, sampleRate, &error);
    } else {
        target = withExtension(path, ".vwt");
        success = m_table.save(target, &error);
    }
    if (!success) {
        m_status("Wavetable save failed: " + error);
        return;
    }
    m_status("Saved wavetable " + baseName(target));
}

void WavetableController::addFrame(bool duplicate) {
    if (!m_table.valid()) return;
    const int selected =
        std::clamp(m_view.selectedFrame, 0, m_table.frameCount() - 1);
    std::vector<float> samples =
        duplicate ? m_table.frame(selected)
                  : std::vector<float>(
                        static_cast<size_t>(m_table.frameSize()), 0.0f);
    double position = 1.0;
    if (m_table.frameCount() > 1) {
        const double current = m_table.framePosition(selected);
        const double next =
            selected + 1 < m_table.frameCount()
                ? m_table.framePosition(selected + 1)
                : 1.0;
        position = std::clamp((current + next) * 0.5, 0.0, 1.0);
    }
    if (!m_table.addKeyframe(samples, position)) {
        m_status("Cannot add wavetable frame");
        return;
    }
    m_weights.push_back(1.0);
    m_view.selectedFrame = m_table.frameCount() - 1;
    syncWeights();
    m_status(duplicate ? "Duplicated wavetable frame"
                       : "Added wavetable frame");
}

void WavetableController::addHarmonicFrame(int harmonics) {
    if (!m_table.valid()) return;
    const int frameSize = m_table.frameSize();
    harmonics = std::clamp(harmonics, 1, std::min(128, frameSize / 2));
    std::vector<double> amplitudes(static_cast<size_t>(harmonics + 1), 0.0);
    for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
        amplitudes[static_cast<size_t>(harmonic)] =
            1.0 / static_cast<double>(harmonic);
    vwave::Wavetable generated(frameSize);
    generated.addFrame(
        std::vector<float>(static_cast<size_t>(frameSize), 0.0f));
    if (!generated.setFrameFromHarmonics(0, amplitudes)) {
        m_status("Cannot generate harmonic frame");
        return;
    }
    generated.normalizeFrames(0.9f);
    const int selected =
        std::clamp(m_view.selectedFrame, 0, m_table.frameCount() - 1);
    double position = 1.0;
    if (m_table.frameCount() > 1) {
        const double current = m_table.framePosition(selected);
        const double next =
            selected + 1 < m_table.frameCount()
                ? m_table.framePosition(selected + 1)
                : 1.0;
        position = std::clamp((current + next) * 0.5, 0.0, 1.0);
    }
    if (!m_table.addKeyframe(generated.frame(0), position)) {
        m_status("Cannot add harmonic frame");
        return;
    }
    m_weights.push_back(1.0);
    m_view.selectedFrame = m_table.frameCount() - 1;
    syncWeights();
    m_status("Added " + std::to_string(harmonics) + "-harmonic frame");
}

void WavetableController::deleteFrame() {
    if (!m_table.valid()) return;
    if (m_table.frameCount() <= 1) {
        m_status("Keep at least one wavetable frame");
        return;
    }
    const int selected =
        std::clamp(m_view.selectedFrame, 0, m_table.frameCount() - 1);
    if (!m_table.removeFrame(selected)) return;
    if (selected < static_cast<int>(m_weights.size()))
        m_weights.erase(m_weights.begin() + selected);
    syncWeights();
    m_status("Removed wavetable frame");
}

void WavetableController::cycleMorphMode() {
    if (!m_table.valid()) return;
    const int next = (static_cast<int>(m_table.morphMode()) + 1) % 5;
    m_table.setMorphMode(static_cast<vwave::WavetableMorphMode>(next));
}

void WavetableController::cycleInterpolation() {
    if (!m_table.valid()) return;
    const int next = (static_cast<int>(m_table.interpolation()) + 1) % 3;
    m_table.setInterpolation(
        static_cast<vwave::WavetableInterpolation>(next));
}

void WavetableController::cycleOversampling() {
    if (!m_table.valid()) return;
    int index = 0;
    for (int i = 0; i < static_cast<int>(sizeof(ui::kWavetableOversampling) /
                                         sizeof(ui::kWavetableOversampling[0]));
         ++i) {
        if (ui::kWavetableOversampling[i] == m_table.oversampling()) {
            index = i;
            break;
        }
    }
    index = (index + 1) %
            static_cast<int>(sizeof(ui::kWavetableOversampling) /
                             sizeof(ui::kWavetableOversampling[0]));
    m_table.setOversampling(ui::kWavetableOversampling[index]);
}

void WavetableController::render(const WavetableNoteParams& params) {
    if (!m_table.valid()) {
        m_status("Extract a wavetable first");
        return;
    }
    vwave::SynthNoteOptions options;
    options.sampleRate = 48000;
    options.channels = 2;
    options.rootNote = params.rootNote;
    options.gain = params.gain;
    options.morph = params.morph;
    options.unison = params.unison;
    if (m_freeBlend &&
        static_cast<int>(m_weights.size()) == m_table.frameCount()) {
        options.blendWeights = m_weights;
    }
    const vwave::Wavetable table = m_table;
    const float note = params.note;
    const double duration = params.duration;
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    m_job = m_jobs.submit(
        [this, request, table, note, duration, options](
            vwave::JobControl& control) mutable {
            std::string error;
            vwave::Audio rendered;
            try {
                rendered =
                    vwave::renderWavetableNote(table, note, duration, options);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post([this, request, rendered = std::move(rendered),
                               error = std::move(error)]() mutable {
                if (request != m_request) return;
                if (rendered.empty()) {
                    m_status(error.empty() ? "Synth render failed" : error);
                    return;
                }
                m_player.stop();
                std::string addError;
                if (!m_timeline.addAudioTrack("Synth", rendered, 0,
                                              &addError)) {
                    m_status("Cannot add synth track: " + addError);
                    return;
                }
                m_timeline.setActiveTrack(m_timeline.trackCount() - 1);
                m_rendered();
                m_status("Rendered wavetable note as a new track");
            });
        });
    m_status("Rendering wavetable note...");
}

void WavetableController::cancel() {
    ++m_request;
    if (m_job.valid()) m_job.cancel();
}

} // namespace vwave::app
