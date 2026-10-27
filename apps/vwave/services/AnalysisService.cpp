#include "services/AnalysisService.h"

#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vwave::app {

AnalysisService::~AnalysisService() {
    cancelAll();
    m_overviewJob.wait();
    m_spectrogramJob.wait();
    m_phaseJob.wait();
}

void AnalysisService::requestOverview(
    std::shared_ptr<const vwave::Audio> audio, int bins,
    std::function<void(std::vector<vwave::PeakBin>)> callback) {
    const uint64_t token = ++m_overviewToken;
    if (m_overviewJob.valid()) m_overviewJob.cancel();
    m_overviewJob = m_jobs.submit(
        [this, token, audio = std::move(audio), bins,
         callback = std::move(callback)](vwave::JobControl& control) mutable {
            if (!audio || audio->empty() || control.cancelled()) return;
            auto peaks = vwave::computePeaks(
                *audio, 0, {0, audio->numFrames()}, bins);
            if (control.cancelled()) return;
            m_dispatcher.post([this, token, peaks = std::move(peaks),
                               callback = std::move(callback)]() mutable {
                if (token != m_overviewToken) return;
                callback(std::move(peaks));
            });
        });
}

void AnalysisService::requestSpectrogram(
    std::shared_ptr<const vwave::Audio> audio, int fftSize, int hop,
    double minHz, double maxHz, bool logarithmic,
    std::function<void(SpectrogramRender)> callback) {
    const uint64_t token = ++m_spectrogramToken;
    if (m_spectrogramJob.valid()) m_spectrogramJob.cancel();
    m_spectrogramJob = m_jobs.submit(
        [this, token, audio = std::move(audio), fftSize, hop, minHz, maxHz,
         logarithmic, callback = std::move(callback)](
            vwave::JobControl& control) mutable {
            if (!audio || audio->empty() || control.cancelled()) return;
            SpectrogramRender render;
            render.spectrogram =
                vwave::computeSpectrogram(*audio, fftSize, hop);
            if (render.spectrogram.frameCount <= 0 ||
                control.cancelled())
                return;
            render.width = std::min(render.spectrogram.frameCount, 2048);
            render.height = 512;
            render.pixels.resize(
                static_cast<size_t>(render.width) *
                static_cast<size_t>(render.height));
            const double ratio =
                std::max(1.0, maxHz / std::max(1.0, minHz));
            for (int y = 0; y < render.height; ++y) {
                const double normalizedY =
                    (static_cast<double>(y) + 0.5) /
                    static_cast<double>(render.height);
                const double frequency =
                    logarithmic
                        ? minHz * std::pow(ratio, 1.0 - normalizedY)
                        : maxHz - (maxHz - minHz) * normalizedY;
                const int bin = std::clamp(
                    static_cast<int>(std::llround(
                        frequency * render.spectrogram.fftSize /
                        std::max(1, render.spectrogram.sampleRate))),
                    0, render.spectrogram.binCount - 1);
                for (int x = 0; x < render.width; ++x) {
                    const int frame = static_cast<int>(
                        static_cast<double>(x) /
                        std::max(1, render.width - 1) *
                        static_cast<double>(
                            render.spectrogram.frameCount - 1));
                    const float db =
                        render.spectrogram.dbAt(frame, bin);
                    const float normalized =
                        std::clamp((db + 90.0f) / 90.0f, 0.0f, 1.0f);
                    render.pixels[static_cast<size_t>(y) *
                                      static_cast<size_t>(render.width) +
                                  static_cast<size_t>(x)] =
                        vwave::ui::makePixel(normalized, 1.0f);
                }
            }
            if (control.cancelled()) return;
            m_dispatcher.post([this, token, render = std::move(render),
                               callback = std::move(callback)]() mutable {
                if (token != m_spectrogramToken) return;
                callback(std::move(render));
            });
        });
}

void AnalysisService::requestPhase(
    std::shared_ptr<const vwave::Audio> audio,
    std::function<void(vwave::PhaseSpectrogram)> callback) {
    const uint64_t token = ++m_phaseToken;
    if (m_phaseJob.valid()) m_phaseJob.cancel();
    m_phaseJob = m_jobs.submit(
        [this, token, audio = std::move(audio),
         callback = std::move(callback)](vwave::JobControl& control) mutable {
            if (!audio || audio->empty() || control.cancelled()) return;
            auto phase = vwave::computePhaseSpectrogram(*audio, 2048, 1536);
            if (control.cancelled()) return;
            m_dispatcher.post([this, token, phase = std::move(phase),
                               callback = std::move(callback)]() mutable {
                if (token != m_phaseToken) return;
                callback(std::move(phase));
            });
        });
}

void AnalysisService::cancelAll() {
    ++m_overviewToken;
    ++m_spectrogramToken;
    ++m_phaseToken;
    if (m_overviewJob.valid()) m_overviewJob.cancel();
    if (m_spectrogramJob.valid()) m_spectrogramJob.cancel();
    if (m_phaseJob.valid()) m_phaseJob.cancel();
}

} // namespace vwave::app
