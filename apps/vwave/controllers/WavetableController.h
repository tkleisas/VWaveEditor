#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/Audio.h"
#include "vwave/JobSystem.h"
#include "vwave/Synth.h"
#include "vwave/Timeline.h"
#include "vwave/UiModel.h"
#include "vwave/Wavetable.h"

#include <functional>
#include <string>
#include <vector>

namespace vwave::app {

struct WavetableNoteParams {
    float note = 60.0f;
    double rootNote = 60.0;
    double duration = 2.0;
    double morph = 0.0;
    double gain = 0.7;
    int unison = 1;
};

class WavetableController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using RenderSink = std::function<void()>;

    WavetableController(vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                        vwave::Timeline& timeline, AudioPlayer& player,
                        StatusSink status, RenderSink rendered);

    vwave::Wavetable& table() { return m_table; }
    const vwave::Wavetable& table() const { return m_table; }
    vwave::WavetableViewport& view() { return m_view; }
    const vwave::WavetableViewport& view() const { return m_view; }
    std::vector<double>& weights() { return m_weights; }
    const std::vector<double>& weights() const { return m_weights; }
    bool freeBlend() const { return m_freeBlend; }
    bool valid() const { return m_table.valid(); }

    void toggleFreeBlend() { m_freeBlend = !m_freeBlend; }
    void clear();
    void syncWeights();
    void extract(const vwave::Audio& audio,
                 const vwave::WavetableExtractOptions& options);
    bool load(const std::string& path);
    void save(const std::string& path, int sampleRate) const;
    void addFrame(bool duplicate);
    void addHarmonicFrame(int harmonics);
    void deleteFrame();
    void cycleMorphMode();
    void cycleInterpolation();
    void cycleOversampling();
    void render(const WavetableNoteParams& params);
    void cancel();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    StatusSink m_status;
    RenderSink m_rendered;
    vwave::Wavetable m_table;
    vwave::WavetableViewport m_view;
    std::vector<double> m_weights;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
    bool m_freeBlend = false;
};

} // namespace vwave::app
