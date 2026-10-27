#include "controllers/ClipController.h"

#include <algorithm>
#include <utility>

namespace vwave::app {

ClipController::ClipController(vwave::Timeline& timeline, int64_t& playhead,
                               vwave::ParameterSlider& stretchRatio,
                               EditRunner runEdit, StatusSink status)
    : m_timeline(timeline),
      m_playhead(playhead),
      m_stretchRatio(stretchRatio),
      m_runEdit(std::move(runEdit)),
      m_status(std::move(status)) {}

vwave::Document* ClipController::activeDocument() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return nullptr;
    return &m_timeline.track(index).document;
}

const vwave::Document* ClipController::activeDocument() const {
    const int index = m_timeline.activeTrack();
    if (index < 0) return nullptr;
    return &m_timeline.track(index).document;
}

void ClipController::reset() {
    m_toolEnabled = false;
    m_selected = -1;
}

void ClipController::onTrackChanged(bool hasClips) {
    m_selected = -1;
    if (!hasClips) m_toolEnabled = false;
}

void ClipController::afterEdit(const vwave::Document& document) {
    if (!document.hasClips()) {
        m_toolEnabled = false;
        m_selected = -1;
        return;
    }
    const int count = static_cast<int>(document.clips().size());
    if (m_selected >= count) m_selected = count - 1;
    if (m_selected < -1) m_selected = -1;
}

void ClipController::toggleTool() {
    auto* document = activeDocument();
    if (!document || !document->hasClips()) {
        m_status("Make clips first");
        return;
    }
    m_toolEnabled = !m_toolEnabled;
    if (m_toolEnabled) {
        m_timeline.clearSelection();
        m_selected = -1;
        m_selected = currentIndex();
    }
    m_status(m_toolEnabled ? "Clip tool enabled" : "Clip tool disabled");
}

void ClipController::make() {
    auto* document = activeDocument();
    if (!document || document->audio().empty()) {
        m_status("Open a track first");
        return;
    }
    if (document->hasClips()) {
        m_status("Track already has clips");
        return;
    }
    m_runEdit([document](std::string* error) {
        return document->createClipArrangement(error);
    }, "Created clip arrangement");
    if (document->hasClips()) {
        m_selected = 0;
        m_toolEnabled = true;
        m_timeline.clearSelection();
    }
}

void ClipController::flatten() {
    auto* document = activeDocument();
    if (!document || !document->hasClips()) {
        m_status("Track has no clips");
        return;
    }
    m_runEdit([document](std::string* error) {
        return document->flattenClips(error);
    }, "Flattened clip arrangement");
    m_selected = -1;
    m_toolEnabled = false;
}

void ClipController::splitAtPlayhead() {
    auto* document = activeDocument();
    if (!document || !document->hasClips()) {
        m_status("Make clips first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t frame =
        std::max<int64_t>(0, m_playhead - track.startFrame);
    m_runEdit([document, frame](std::string* error) {
        return document->splitClipAt(frame, error);
    }, "Split clip");
    m_selected = document->clipIndexAt(frame);
}

void ClipController::deleteSelected() {
    auto* document = activeDocument();
    const int index = currentIndex();
    if (!document || index < 0) {
        m_status("Select a clip first");
        return;
    }
    m_runEdit([document, index](std::string* error) {
        return document->deleteClip(static_cast<size_t>(index), error);
    }, "Deleted clip");
    afterEdit(*document);
}

void ClipController::repeatSelected(int count) {
    auto* document = activeDocument();
    const int index = currentIndex();
    if (!document || index < 0) {
        m_status("Select a clip first");
        return;
    }
    count = std::clamp(count, 1, 64);
    m_runEdit([document, index, count](std::string* error) {
        return document->repeatClip(static_cast<size_t>(index), count, error);
    }, "Repeated clip");
    m_selected = index;
}

void ClipController::moveSelectedToPlayhead() {
    auto* document = activeDocument();
    const int index = currentIndex();
    if (!document || index < 0) {
        m_status("Select a clip first");
        return;
    }
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const int64_t frame =
        std::max<int64_t>(0, m_playhead - track.startFrame);
    m_runEdit([document, index, frame](std::string* error) {
        return document->moveClip(static_cast<size_t>(index), frame, error);
    }, "Moved clip to playhead");
    m_selected = index;
}

void ClipController::stretchSelected() {
    auto* document = activeDocument();
    const int index = currentIndex();
    if (!document || index < 0) {
        m_status("Select a clip first");
        return;
    }
    m_runEdit([document, index, this](std::string* error) {
        return document->stretchClip(static_cast<size_t>(index),
                                     m_stretchRatio.value, error);
    }, "Stretched clip");
    m_selected = index;
}

int ClipController::currentIndex() const {
    const auto* document = activeDocument();
    if (!document) return -1;
    const auto& clips = document->clips();
    if (clips.empty()) return -1;
    if (m_selected >= 0 && m_selected < static_cast<int>(clips.size()))
        return m_selected;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    int index = document->clipIndexAt(m_playhead - track.startFrame);
    if (index < 0 && m_timeline.selection().active) {
        index = document->clipIndexAt(m_timeline.selection().start -
                                      track.startFrame);
    }
    return index;
}

} // namespace vwave::app
