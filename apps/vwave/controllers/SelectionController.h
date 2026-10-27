#pragma once

#include "AudioPlayer.h"
#include "vwave/Timeline.h"
#include "vwave/UiModel.h"

#include <cstdint>
#include <functional>
#include <string>

namespace vwave::app {

class SelectionController {
public:
    using EditRunner = std::function<void(
        const std::function<bool(std::string*)>&, const std::string&)>;
    using ChangeSink = std::function<void(bool documentEdited)>;
    using StatusSink = std::function<void(const std::string&)>;

    SelectionController(vwave::Timeline& timeline, AudioPlayer& player,
                        int64_t& playhead,
                        vwave::ParameterSlider& stretchRatio,
                        EditRunner runEdit, ChangeSink changed,
                        StatusSink status);

    void toggleScope();
    void cropActiveTrack();
    void shiftActiveTrack(double seconds);
    void repeatSelection(int count);
    void moveSelectionToPlayhead(bool copy);
    void zeroCrossCut();
    void snapSelection();
    void stretchTrack(vwave::StretchAlgorithm algorithm);
    void addMarkerAtPlayhead();

private:
    vwave::Document* activeDocument();

    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    int64_t& m_playhead;
    vwave::ParameterSlider& m_stretchRatio;
    EditRunner m_runEdit;
    ChangeSink m_changed;
    StatusSink m_status;
};

} // namespace vwave::app
