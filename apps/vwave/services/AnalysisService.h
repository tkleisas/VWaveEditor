#pragma once

#include "services/UiDispatcher.h"

#include "vwave/Analysis.h"
#include "vwave/Audio.h"
#include "vwave/JobSystem.h"
#include "vwave/Spectral.h"
#include "vwave/UiModel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace vwave::app {

class AnalysisService {
public:
    struct SpectrogramRender {
        vwave::Spectrogram spectrogram;
        std::vector<uint32_t> pixels;
        int width = 0;
        int height = 0;
    };

    AnalysisService(vwave::JobSystem& jobs, UiDispatcher& dispatcher)
        : m_jobs(jobs), m_dispatcher(dispatcher) {}
    ~AnalysisService();

    void requestOverview(std::shared_ptr<const vwave::Audio> audio, int bins,
                         std::function<void(std::vector<vwave::PeakBin>)>
                             callback);
    void requestSpectrogram(std::shared_ptr<const vwave::Audio> audio,
                            int fftSize, int hop, double minHz, double maxHz,
                            bool logarithmic,
                            std::function<void(SpectrogramRender)> callback);
    void requestPhase(std::shared_ptr<const vwave::Audio> audio,
                      std::function<void(vwave::PhaseSpectrogram)> callback);
    void requestBeats(std::shared_ptr<const vwave::Audio> audio,
                      double sensitivity,
                      std::function<void(vwave::BeatAnalysis)> callback);
    void requestPitch(std::shared_ptr<const vwave::Audio> audio,
                      vwave::Range range,
                      std::function<void(vwave::PitchEstimate)> callback);
    void cancelAll();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::JobHandle m_overviewJob;
    vwave::JobHandle m_spectrogramJob;
    vwave::JobHandle m_phaseJob;
    vwave::JobHandle m_beatJob;
    vwave::JobHandle m_pitchJob;
    uint64_t m_overviewToken = 0;
    uint64_t m_spectrogramToken = 0;
    uint64_t m_phaseToken = 0;
    uint64_t m_beatToken = 0;
    uint64_t m_pitchToken = 0;
};

} // namespace vwave::app
