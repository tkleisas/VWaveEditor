#pragma once

#include "vwave/Timeline.h"
#include "vwave/UiModel.h"

#include <cstdint>
#include <functional>
#include <string>

namespace vwave::app {

class ClipController {
public:
    using EditRunner = std::function<void(
        const std::function<bool(std::string*)>&, const std::string&)>;
    using StatusSink = std::function<void(const std::string&)>;

    ClipController(vwave::Timeline& timeline, int64_t& playhead,
                   vwave::ParameterSlider& stretchRatio, EditRunner runEdit,
                   StatusSink status);

    bool toolEnabled() const { return m_toolEnabled; }
    int selectedClip() const { return m_selected; }
    void setSelectedClip(int index) { m_selected = index; }

    void reset();
    void onTrackChanged(bool hasClips);
    void afterEdit(const vwave::Document& document);
    void toggleTool();
    void make();
    void flatten();
    void splitAtPlayhead();
    void deleteSelected();
    void repeatSelected(int count);
    void moveSelectedToPlayhead();
    void stretchSelected();
    int currentIndex() const;

private:
    vwave::Document* activeDocument();
    const vwave::Document* activeDocument() const;

    vwave::Timeline& m_timeline;
    int64_t& m_playhead;
    vwave::ParameterSlider& m_stretchRatio;
    EditRunner m_runEdit;
    StatusSink m_status;
    bool m_toolEnabled = false;
    int m_selected = -1;
};

} // namespace vwave::app
