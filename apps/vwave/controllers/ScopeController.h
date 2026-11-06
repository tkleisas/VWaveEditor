#pragma once

#include "AudioPlayer.h"
#include "services/UiDispatcher.h"
#include "vwave/JobSystem.h"
#include "vwave/Oscilloscope.h"
#include "vwave/ScopeComposition.h"
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
    using PreviewSink = std::function<void(vwave::Audio audio)>;

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
    bool pointEditing() const {
        return m_shape == 0 && m_pointEditing;
    }
    bool animated() const { return m_keyframes.size() >= 2; }
    const std::vector<vwave::ScopeKeyframe>& keyframes() const {
        return m_keyframes;
    }
    const std::vector<vwave::ScopeDrawing>& drawings() const {
        return m_drawings;
    }
    int selectedKeyframe() const { return m_selectedKeyframe; }
    void setSelectedKeyframe(int index);
    vwave::ScopeTransform selectedTransform() const;
    vwave::ScopeTransform transformAt(double time) const;
    vwave::ScopeEasing selectedEasing() const;
    vwave::ScopeTransition selectedTransition() const;
    double selectedKeyframeTime() const;
    double duration() const { return m_duration; }
    void setDuration(double duration);
    void addKeyframe(double time);
    void deleteSelectedKeyframe();
    void moveSelectedKeyframe(double time);
    void setSelectedTransform(const vwave::ScopeTransform& transform);
    void cycleSelectedEasing();
    void cycleSelectedTransition();
    vwave::ScopeComposition composition() const;

    void generate(const ScopeShapeParams& params);
    void cycleShape(const ScopeShapeParams& params);
    void cycleDirection();
    void cycleOversampling();
    void toggleBandLimited() { m_bandLimited = !m_bandLimited; }
    void toggleNormalize() { m_normalize = !m_normalize; }
    void setPath(vwave::ScopePath path);
    void commitPathToKeyframe();
    void startPolyline();
    void importImage(const std::string& path);
    void importSvg(const std::string& path);
    bool load(const std::string& path);
    void save(const std::string& path) const;
    void render(int64_t playhead, const ScopeRenderParams& params);
    void renderPreview(const ScopeRenderParams& params, PreviewSink preview);
    void cancel();

private:
    const vwave::ScopeDrawing* drawingById(const std::string& id) const;
    vwave::ScopeDrawing* drawingById(const std::string& id);
    std::string allocateDrawingId();
    void loadDrawingForKeyframe(int index);
    void bindPathToKeyframe();

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
    bool m_pointEditing = false;
    std::vector<vwave::ScopeKeyframe> m_keyframes;
    std::vector<vwave::ScopeDrawing> m_drawings;
    std::string m_activeDrawingId;
    uint64_t m_nextDrawingId = 1;
    int m_selectedKeyframe = -1;
    double m_duration = 2.0;
};

} // namespace vwave::app
