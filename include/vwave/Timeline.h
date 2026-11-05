#pragma once

#include "vwave/Audio.h"
#include "vwave/Document.h"
#include "vwave/HistoryStamp.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

enum class SelectionScope {
    AllTracks,
    ActiveTrack,
};

enum class StereoPadding {
    Back,
    Front,
    Both,
};

struct StereoCombineOptions {
    StereoPadding padding = StereoPadding::Back;
    bool keepOriginals = true;
};

struct TimelineTrack {
    std::string name;
    Document document;
    nlohmann::json recipe;
    int64_t startFrame = 0;
    double gainDb = 0.0;
    double pan = 0.0;
    bool muted = false;
    bool solo = false;
    bool armed = false;
    bool empty = false;
};

class Timeline {
public:
    Timeline() = default;

    bool empty() const { return m_tracks.empty(); }
    int trackCount() const { return static_cast<int>(m_tracks.size()); }
    int sampleRate() const { return m_sampleRate; }
    int64_t lengthFrames() const;
    double duration() const;
    int activeTrack() const { return m_active; }
    void setActiveTrack(int index);
    SelectionScope selectionScope() const { return m_selectionScope; }
    void setSelectionScope(SelectionScope scope);

    TimelineTrack& track(int index);
    const TimelineTrack& track(int index) const;

    bool addTrack(std::string name, Document document, int64_t startFrame = 0,
                  std::string* error = nullptr);
    bool addAudioTrack(std::string name, const Audio& audio,
                       int64_t startFrame = 0,
                       std::string* error = nullptr);
    bool addEmptyTrack(std::string name, int64_t startFrame = 0,
                       std::string* error = nullptr);
    bool setTrackAudio(int index, Audio audio, int64_t startFrame = 0,
                       std::string* error = nullptr);
    bool removeTrack(int index);

    bool setTrackName(int index, std::string name);
    bool setTrackGainDb(int index, double db);
    bool setTrackPan(int index, double pan);
    bool setTrackMuted(int index, bool muted);
    bool setTrackSolo(int index, bool solo);
    bool setTrackArmed(int index, bool armed);
    bool setTrackStartFrame(int index, int64_t frame);
    bool anySolo() const;
    int armedTrack() const;

    void setSelection(int64_t start, int64_t end);
    void clearSelection();
    const Selection& selection() const { return m_selection; }
    Range selectedRange() const;
    Range activeRange() const;

    bool cropTrack(int index, Range range = {},
                   std::string* error = nullptr);
    bool shiftTrack(int index, int64_t deltaFrames,
                    std::string* error = nullptr);
    bool splitStereoTrack(int index, bool keepOriginal = true,
                          std::string* error = nullptr);
    bool combineMonoTracks(int leftIndex, int rightIndex,
                           StereoCombineOptions options = {},
                           std::string* error = nullptr);

    Audio mixdown(Range range = {}) const;

    bool canUndo() const;
    bool canRedo() const;
    size_t undoCount() const;
    size_t redoCount() const;
    bool undo();
    bool redo();
    std::string undoLabel() const;
    std::string redoLabel() const;
    void clearUndoHistory();

    nlohmann::json toJson() const;
    bool saveProject(const std::string& path,
                     std::string* error = nullptr) const;
    bool loadProject(const std::string& path, std::string* error = nullptr);

private:
    struct TimelineState {
        std::vector<TimelineTrack> tracks;
        int sampleRate = 0;
        int active = -1;
        Selection selection;
        SelectionScope selectionScope = SelectionScope::AllTracks;
    };

    struct UndoEntry {
        HistoryStamp stamp;
        TimelineState state;
        std::string label;
        std::string coalesceKey;
    };

    static constexpr size_t kMaxUndoEntries = 64;

    TimelineState captureState() const;
    void restoreState(TimelineState state);
    bool recordUndo(const std::string& label,
                    const std::string& coalesceKey = {});
    void recordUndoAfter(TimelineState before, const std::string& label,
                         const std::string& coalesceKey = {});
    bool undoTimeline();
    bool redoTimeline();
    void discardStaleTimelineRedo() const;
    void syncSelectionFromActiveDocument();
    bool latestDocumentUndo(int& index, HistoryStamp& stamp) const;
    bool latestDocumentRedo(int& index, HistoryStamp& stamp) const;

    bool validateSampleRate(const Audio& audio, std::string* error) const;
    void syncSelectionToTracks();
    bool trackAudible(const TimelineTrack& track) const;

    std::vector<TimelineTrack> m_tracks;
    int m_sampleRate = 0;
    int m_active = -1;
    Selection m_selection;
    SelectionScope m_selectionScope = SelectionScope::AllTracks;
    std::vector<UndoEntry> m_undo;
    mutable std::vector<UndoEntry> m_redo;
};

const char* stereoPaddingName(StereoPadding padding);
bool stereoPaddingFromName(const std::string& name, StereoPadding& padding);

} // namespace vwave
