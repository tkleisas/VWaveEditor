#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/JobSystem.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Timeline.h"

#include <functional>
#include <string>

namespace vwave::app {

struct ScopeShapeParams {
    int pointCount = 256;
    double size = 0.8;
    double a = 3.0;
    double b = 2.0;
    double phase = 0.0;
    int turns = 4;
};

struct ScopeRenderParams {
    double duration = 2.0;
    double scanHz = 120.0;
    double gain = 0.8;
    double smoothing = 0.0;
};

class ScopeController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using RenderSink = std::function<void(int64_t start)>;

    ScopeController(vwave::JobSystem& jobs, UiDispatcher& dispatcher,
                    vwave::Timeline& timeline, AudioPlayer& player,
                    StatusSink status, RenderSink rendered);

    const vwave::ScopePath& path() const { return m_path; }
    vwave::ScopePath& path() { return m_path; }
    bool valid() const { return m_path.valid(); }
    int shape() const { return m_shape; }
    int direction() const { return m_direction; }
    int oversampling() const { return m_oversampling; }
    bool bandLimited() const { return m_bandLimited; }
    bool normalize() const { return m_normalize; }

    void generate(const ScopeShapeParams& params);
    void cycleShape(const ScopeShapeParams& params);
    void cycleDirection();
    void cycleOversampling();
    void toggleBandLimited() { m_bandLimited = !m_bandLimited; }
    void toggleNormalize() { m_normalize = !m_normalize; }
    void setPath(vwave::ScopePath path);
    void startPolyline();
    bool load(const std::string& path);
    void save(const std::string& path) const;
    void render(int64_t playhead, const ScopeRenderParams& params);
    void cancel();

private:
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    StatusSink m_status;
    RenderSink m_rendered;
    vwave::ScopePath m_path;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
    int m_shape = 2;
    int m_direction = 0;
    int m_oversampling = 2;
    bool m_bandLimited = true;
    bool m_normalize = true;
};

} // namespace vwave::app
