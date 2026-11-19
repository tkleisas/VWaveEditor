#include "controllers/SpectralController.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace vwave::app {

namespace {

constexpr double kPreviewSeconds = 6.0;
constexpr int64_t kMinimumPreviewFrames = 512;

struct Band {
    double low = 0.0;
    double high = 0.0;
};

Band clampedBand(const vwave::Audio& audio, double lowHz, double highHz) {
    const double maxHz =
        std::max(1000.0, static_cast<double>(audio.sampleRate()) / 2.0);
    const double low = std::clamp(lowHz, 1.0, maxHz - 1.0);
    const double high = std::clamp(highHz, low + 1.0, maxHz);
    return {low, high};
}

vwave::Audio renderPreviewAudio(const vwave::Audio& audio,
                                const SpectralPreviewParams& params) {
    vwave::SpectralEditSettings edit;
    switch (params.kind) {
    case SpectralEditKind::Gain:
        edit.type = vwave::SpectralEditType::Gain;
        break;
    case SpectralEditKind::Gate:
        edit.type = vwave::SpectralEditType::Gate;
        break;
    case SpectralEditKind::Filter:
        edit.type = vwave::SpectralEditType::Filter;
        break;
    case SpectralEditKind::Blur:
        edit.type = vwave::SpectralEditType::Blur;
        break;
    }
    edit.lowHz = params.lowHz;
    edit.highHz = params.highHz;
    edit.gainDb = params.gainDb;
    edit.thresholdDb = params.thresholdDb;
    edit.cutoffHz = params.cutoffHz;
    edit.lowpass = params.lowpass;
    edit.blurTime = params.blurTime;
    edit.blurFrequency = params.blurFrequency;
    return vwave::applySpectralEdit(audio, edit);
}

} // namespace

const char* spectralEditKindName(SpectralEditKind kind) {
    switch (kind) {
    case SpectralEditKind::Gain: return "Gain";
    case SpectralEditKind::Gate: return "Gate";
    case SpectralEditKind::Filter: return "Filter";
    case SpectralEditKind::Blur: return "Blur";
    }
    return "Gain";
}

SpectralController::SpectralController(vwave::Timeline& timeline,
                                       AudioPlayer& player,
                                       vwave::JobSystem& jobs,
                                       vwave::app::UiDispatcher& dispatcher,
                                       EditRunner runEdit,
                                       StatusSink status)
    : m_timeline(timeline),
      m_player(player),
      m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_runEdit(std::move(runEdit)),
      m_status(std::move(status)) {}

SpectralController::~SpectralController() {
    ++m_previewToken;
    if (m_previewJob.valid()) {
        m_previewJob.cancel();
        m_previewJob.wait();
    }
    if (m_previewPlaying) m_player.stop();
}

vwave::Document* SpectralController::activeDocument() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return nullptr;
    return &m_timeline.track(index).document;
}

void SpectralController::clear() {
    m_region.clear();
    cancelPreviewJob();
    stopPreviewPlayback();
    m_previewPending = false;
    m_previewRange = {};
    m_previewClipped = false;
    m_previewFrames = 0;
    m_dryAudio.reset();
    m_wetAudio.reset();
}

void SpectralController::setBand(double lowHz, double highHz) {
    if (!m_region.active) return;
    const auto* document = activeDocument();
    const double maxHz =
        document && document->sampleRate() > 0
            ? std::max(1000.0,
                       static_cast<double>(document->sampleRate()) / 2.0)
            : 20000.0;
    const double low = std::clamp(lowHz, 1.0, maxHz - 1.0);
    const double high = std::clamp(highHz, low + 1.0, maxHz);
    m_region.lowHz = low;
    m_region.highHz = high;
}

vwave::Range SpectralController::timeRange() const {
    if (m_region.active && !m_region.frames.empty())
        return m_region.frames;
    const int index = m_timeline.activeTrack();
    if (index < 0) return {};
    const auto& document = m_timeline.track(index).document;
    if (document.selection().active) return document.selectedRange();
    return {0, document.numFrames()};
}

void SpectralController::applyGain(double lowHz, double highHz, double db) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    const Band band = clampedBand(document->audio(), lowHz, highHz);
    const double low = band.low;
    const double high = band.high;
    if (m_region.active) {
        m_region.lowHz = low;
        m_region.highHz = high;
    }
    stopPreviewPlayback();
    m_runEdit([document, db, range, low, high](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralGain(low, high, db, error);
    }, "Spectral gain applied");
}

void SpectralController::applyFilter(bool lowpass, double cutoffHz) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    const double maxHz = std::max(
        1000.0, static_cast<double>(document->sampleRate()) / 2.0);
    const double cutoff = std::clamp(cutoffHz, 1.0, maxHz);
    stopPreviewPlayback();
    m_runEdit([document, lowpass, cutoff, range](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralFilter(lowpass ? "lowpass" : "highpass",
                                        cutoff, error);
    }, lowpass ? "Spectral lowpass applied" : "Spectral highpass applied");
}

void SpectralController::applyGate(double thresholdDb) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    stopPreviewPlayback();
    m_runEdit([document, thresholdDb, range](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralGate(thresholdDb, error);
    }, "Spectral gate applied");
}

void SpectralController::applyBlur(int timeRadius, int frequencyRadius) {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) return;
    const vwave::Range range = timeRange();
    if (range.empty()) {
        m_status("Select a time range or drag a spectral region first");
        return;
    }
    stopPreviewPlayback();
    m_runEdit([document, timeRadius, frequencyRadius, range](std::string* error) {
        document->setSelection(range.start, range.end);
        return document->spectralBlur(timeRadius, frequencyRadius, error);
    }, "Spectral blur applied");
}

void SpectralController::setPreviewEnabled(bool enabled) {
    if (enabled == m_previewEnabled) return;
    m_previewEnabled = enabled;
    if (enabled) return;
    cancelPreviewJob();
    stopPreviewPlayback();
    m_previewPending = false;
    m_previewRange = {};
    m_previewClipped = false;
    m_previewFrames = 0;
    m_dryAudio.reset();
    m_wetAudio.reset();
}

void SpectralController::cancelPreviewJob() {
    ++m_previewToken;
    if (m_previewJob.valid()) m_previewJob.cancel();
}

void SpectralController::stopPreviewPlayback() {
    if (m_previewPlaying) m_player.stop();
    m_previewPlaying = false;
}

bool SpectralController::playPreviewAudio(
    const std::shared_ptr<vwave::Audio>& audio, int64_t startFrame) {
    if (!audio || audio->empty()) return false;
    m_player.stop();
    const int64_t start =
        std::clamp<int64_t>(startFrame, 0, audio->numFrames() - 1);
    m_previewPlaying =
        m_player.playAudio(audio, start, audio->numFrames(), true);
    return m_previewPlaying;
}

void SpectralController::setPreviewWet(bool wet) {
    if (wet == m_previewWet) return;
    m_previewWet = wet;
    const auto& audio = wet ? m_wetAudio : m_dryAudio;
    if (!m_previewEnabled || !audio || audio->empty()) return;
    const int64_t position =
        m_previewPlaying ? previewPosition() : int64_t{0};
    if (!playPreviewAudio(audio, position)) {
        m_previewEnabled = false;
        m_status("Spectral preview playback unavailable");
        return;
    }
    m_status(wet ? "Preview: wet" : "Preview: dry");
}

int64_t SpectralController::previewPosition() const {
    return m_previewPlaying && m_player.isPlaying()
               ? m_player.sourcePosition()
               : 0;
}

void SpectralController::requestPreview(
    const SpectralPreviewParams& params, int64_t playhead) {
    if (!m_previewEnabled) return;
    auto* document = activeDocument();
    if (!document || document->audio().empty()) {
        m_previewEnabled = false;
        m_previewPending = false;
        m_status("No active audio for spectral preview");
        return;
    }

    const int64_t totalFrames = document->audio().numFrames();
    const bool explicitRange =
        (m_region.active && !m_region.frames.empty()) ||
        document->selection().active;
    const int sampleRate = std::max(1, document->sampleRate());
    const int64_t cap = std::max<int64_t>(
        kMinimumPreviewFrames,
        static_cast<int64_t>(std::llround(
            kPreviewSeconds * static_cast<double>(sampleRate))));

    vwave::Range range = timeRange().clamped(totalFrames);
    int64_t startFrame = 0;
    bool clipped = false;
    if (!explicitRange) {
        const auto& track = m_timeline.track(m_timeline.activeTrack());
        const int64_t localPlayhead = std::clamp<int64_t>(
            playhead - track.startFrame, 0, totalFrames);
        int64_t start = localPlayhead;
        if (start + cap > totalFrames)
            start = std::max<int64_t>(0, totalFrames - cap);
        range = {start, std::min(totalFrames, start + cap)};
        startFrame = std::clamp<int64_t>(
            localPlayhead - range.start, 0,
            std::max<int64_t>(0, range.length() - 1));
    } else if (range.length() > cap) {
        range.end = range.start + cap;
        clipped = true;
    }
    if (range.length() < kMinimumPreviewFrames) {
        m_previewEnabled = false;
        m_previewPending = false;
        m_status("Select at least a few milliseconds to preview");
        return;
    }

    SpectralPreviewParams preview = params;
    const Band band =
        clampedBand(document->audio(), preview.lowHz, preview.highHz);
    preview.lowHz = band.low;
    preview.highHz = band.high;
    const double maxHz =
        std::max(1000.0, static_cast<double>(sampleRate) / 2.0);
    preview.cutoffHz = std::clamp(preview.cutoffHz, 1.0, maxHz);
    preview.blurTime = std::max(0, preview.blurTime);
    preview.blurFrequency = std::max(0, preview.blurFrequency);

    const auto dry =
        std::make_shared<vwave::Audio>(document->audio().slice(range));
    const bool sameRange = m_previewRange.start == range.start &&
                           m_previewRange.end == range.end;
    const int64_t oldPosition = previewPosition();
    const int64_t oldFrames = m_previewFrames;
    const bool wasPlaying = m_previewPlaying;

    cancelPreviewJob();
    const uint64_t token = m_previewToken;
    m_previewPending = true;
    if (!wasPlaying)
        m_status("Rendering spectral preview...");

    m_previewJob = m_jobs.submit(
        [this, token, dry, preview, range, startFrame, clipped,
         sameRange, oldPosition, oldFrames,
         wasPlaying](vwave::JobControl& control) mutable {
            if (control.cancelled()) return;
            vwave::Audio processed;
            try {
                processed = renderPreviewAudio(*dry, preview);
            } catch (const std::exception&) {
                processed = {};
            } catch (...) {
                processed = {};
            }
            if (control.cancelled()) return;
            if (processed.empty()) {
                m_dispatcher.post([this, token] {
                    if (token != m_previewToken) return;
                    m_previewPending = false;
                    m_previewEnabled = false;
                    m_status("Spectral preview failed");
                });
                return;
            }
            m_dispatcher.post(
                [this, token, dry = std::move(dry),
                 processed = std::move(processed), range, startFrame,
                 clipped, sameRange, oldPosition, oldFrames,
                 wasPlaying, preview]() mutable {
                    if (token != m_previewToken || !m_previewEnabled)
                        return;
                    m_previewPending = false;
                    m_previewRange = range;
                    m_previewClipped = clipped;
                    m_previewFrames = processed.numFrames();
                    m_dryAudio = std::move(dry);
                    m_wetAudio =
                        std::make_shared<vwave::Audio>(std::move(processed));

                    int64_t start = startFrame;
                    if (wasPlaying && sameRange && oldFrames > 0 &&
                        oldPosition > 0) {
                        start = static_cast<int64_t>(std::llround(
                            static_cast<double>(oldPosition) *
                            static_cast<double>(m_previewFrames) /
                            static_cast<double>(oldFrames)));
                    }
                    const auto& audio =
                        m_previewWet ? m_wetAudio : m_dryAudio;
                    if (!playPreviewAudio(audio, start)) {
                        m_previewEnabled = false;
                        m_status(
                            "Spectral preview ready; audio playback "
                            "unavailable");
                        return;
                    }
                    std::string message =
                        std::string("Previewing spectral ") +
                        spectralEditKindName(preview.kind);
                    message += m_previewWet ? " (wet)" : " (dry)";
                    if (clipped) message += ", first 6 s";
                    m_status(message);
                });
        });
}

} // namespace vwave::app
