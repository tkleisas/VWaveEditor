#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/JobSystem.h"
#include "vwave/Spectral.h"
#include "vwave/Timeline.h"
#include "vwave/UiModel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace vwave::app {

enum class SpectralEditKind {
    Gain,
    Gate,
    Filter,
    Blur
};

const char* spectralEditKindName(SpectralEditKind kind);

struct SpectralPreviewParams {
    SpectralEditKind kind = SpectralEditKind::Gain;
    double lowHz = 200.0;
    double highHz = 4000.0;
    double gainDb = 6.0;
    double thresholdDb = -40.0;
    double cutoffHz = 1000.0;
    bool lowpass = true;
    int blurTime = 1;
    int blurFrequency = 2;
};

class SpectralController {
public:
    using EditRunner = std::function<void(
        const std::function<bool(std::string*)>&, const std::string&)>;
    using StatusSink = std::function<void(const std::string&)>;

    SpectralController(vwave::Timeline& timeline, AudioPlayer& player,
                       vwave::JobSystem& jobs,
                       vwave::app::UiDispatcher& dispatcher,
                       EditRunner runEdit, StatusSink status);
    ~SpectralController();

    SpectralController(const SpectralController&) = delete;
    SpectralController& operator=(const SpectralController&) = delete;

    vwave::SpectralRegion& region() { return m_region; }
    const vwave::SpectralRegion& region() const { return m_region; }
    void clear();
    void setBand(double lowHz, double highHz);

    vwave::Range timeRange() const;
    void applyGain(double lowHz, double highHz, double db);
    void applyFilter(bool lowpass, double cutoffHz);
    void applyGate(double thresholdDb);
    void applyBlur(int timeRadius, int frequencyRadius);

    void setPreviewEnabled(bool enabled);
    bool previewEnabled() const { return m_previewEnabled; }
    void setPreviewKind(SpectralEditKind kind) { m_previewKind = kind; }
    SpectralEditKind previewKind() const { return m_previewKind; }
    void setPreviewWet(bool wet);
    bool previewWet() const { return m_previewWet; }

    void requestPreview(const SpectralPreviewParams& params,
                        int64_t playhead);
    bool previewPlaying() const { return m_previewPlaying; }
    bool previewPending() const { return m_previewPending; }
    bool previewClipped() const { return m_previewClipped; }
    int64_t previewPosition() const;
    vwave::Range previewRange() const { return m_previewRange; }

private:
    vwave::Document* activeDocument();
    void cancelPreviewJob();
    void stopPreviewPlayback();
    bool playPreviewAudio(const std::shared_ptr<vwave::Audio>& audio,
                          int64_t startFrame);

    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    vwave::JobSystem& m_jobs;
    vwave::app::UiDispatcher& m_dispatcher;
    EditRunner m_runEdit;
    StatusSink m_status;
    vwave::SpectralRegion m_region;

    vwave::JobHandle m_previewJob;
    uint64_t m_previewToken = 0;
    bool m_previewEnabled = false;
    bool m_previewPlaying = false;
    bool m_previewPending = false;
    bool m_previewWet = true;
    bool m_previewClipped = false;
    SpectralEditKind m_previewKind = SpectralEditKind::Gain;
    vwave::Range m_previewRange{};
    int64_t m_previewFrames = 0;
    std::shared_ptr<vwave::Audio> m_dryAudio;
    std::shared_ptr<vwave::Audio> m_wetAudio;
};

} // namespace vwave::app
