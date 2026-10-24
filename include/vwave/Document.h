#pragma once

#include "vwave/Audio.h"
#include "vwave/AudioIO.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

enum class StretchAlgorithm {
    Wsola,
    PhaseVocoder,
    PaulStretch
};

struct Marker {
    int64_t frame = 0;
    std::string name;
};

struct Slice {
    Range range;
    std::string name;
};

class Operation {
public:
    enum class Kind {
        Process,
        Delete,
        Insert,
        ReplaceRange,
        Crop,
        Resample,
        ReplaceAll
    };

    enum class Process {
        Gain,
        Normalize,
        FadeIn,
        FadeOut,
        Reverse,
        Invert,
        Silence,
        DcRemove,
        TimeStretch,
        PitchShift,
        SpectralGain,
        SpectralFilter,
        SpectralGate,
        SpectralBlur,
        Convolve
    };

    Kind kind = Kind::Process;
    Process process = Process::Gain;
    Range range;
    Audio payload;
    int sampleRate = 0;
    nlohmann::json params = nlohmann::json::object();
    std::string label;

    static Operation gain(double db, Range range);
    static Operation normalize(double peakDb, Range range);
    static Operation fadeIn(Range range);
    static Operation fadeOut(Range range);
    static Operation reverse(Range range);
    static Operation invert(Range range);
    static Operation silence(Range range);
    static Operation dcRemove(Range range);
    static Operation timeStretch(double ratio, Range range,
                                 StretchAlgorithm algorithm);
    static Operation pitchShift(double semitones, Range range,
                                StretchAlgorithm algorithm);
    static Operation spectralGain(double lowHz, double highHz, double gainDb,
                                  Range range);
    static Operation spectralFilter(const std::string& type, double cutoffHz,
                                    Range range);
    static Operation spectralGate(double thresholdDb, Range range);
    static Operation spectralBlur(int timeRadius, int frequencyRadius,
                                  Range range);
    static Operation convolve(Audio impulseResponse, Range range,
                              bool normalizeIr);

    static Operation remove(Range range);
    static Operation insert(int64_t frame, Audio audio);
    static Operation replaceRange(Range range, Audio audio);
    static Operation crop(Range range);
    static Operation resample(int sampleRate);
    static Operation replaceAll(Audio audio);

    nlohmann::json toJson() const;
    static bool fromJson(const nlohmann::json& json, Operation& out,
                         std::string* error = nullptr);
};

class Document {
public:
    Document();
    explicit Document(Audio source);

    static Document fromAudio(Audio source);

    bool loadAudio(const std::string& path, std::string* error = nullptr);
    bool saveAudio(const std::string& path,
                   const AudioExportOptions& options = {},
                   std::string* error = nullptr) const;
    bool loadProject(const std::string& path, std::string* error = nullptr);
    bool saveProject(const std::string& path,
                     std::string* error = nullptr) const;

    const Audio& source() const { return m_source; }
    const Audio& audio() const;
    int sampleRate() const;
    int numChannels() const;
    int64_t numFrames() const;
    double duration() const;

    const std::string& sourcePath() const { return m_sourcePath; }
    void setSourcePath(std::string path) { m_sourcePath = std::move(path); }

    const Selection& selection() const { return m_selection; }
    void setSelection(int64_t start, int64_t end);
    void setSelection(const Selection& selection);
    void selectAll();
    void clearSelection();
    Range selectedRange() const;
    Range activeRange() const;

    bool apply(const Operation& operation, std::string* error = nullptr);
    bool undo();
    bool redo();
    bool canUndo() const { return !m_history.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    size_t undoCount() const { return m_history.size(); }
    size_t redoCount() const { return m_redo.size(); }
    std::string undoLabel() const;
    std::string redoLabel() const;
    std::vector<Operation> operationHistory() const;
    void clearHistory();

    bool gainDb(double db, std::string* error = nullptr);
    bool normalize(double peakDb = -1.0, std::string* error = nullptr);
    bool fadeIn(std::string* error = nullptr);
    bool fadeOut(std::string* error = nullptr);
    bool reverseSelection(std::string* error = nullptr);
    bool invertSelection(std::string* error = nullptr);
    bool silenceSelection(std::string* error = nullptr);
    bool removeDc(std::string* error = nullptr);
    bool timeStretch(double ratio, StretchAlgorithm algorithm,
                     std::string* error = nullptr);
    bool pitchShift(double semitones, StretchAlgorithm algorithm,
                    std::string* error = nullptr);
    bool spectralGain(double lowHz, double highHz, double gainDb,
                      std::string* error = nullptr);
    bool spectralFilter(const std::string& type, double cutoffHz,
                        std::string* error = nullptr);
    bool spectralGate(double thresholdDb, std::string* error = nullptr);
    bool spectralBlur(int timeRadius, int frequencyRadius,
                      std::string* error = nullptr);
    bool convolve(const Audio& impulseResponse, bool normalizeIr,
                  std::string* error = nullptr);

    bool copySelection();
    bool cutSelection(std::string* error = nullptr);
    bool deleteSelection(std::string* error = nullptr);
    bool paste(std::string* error = nullptr);
    bool pasteAt(int64_t frame, std::string* error = nullptr);
    bool cropToSelection(std::string* error = nullptr);
    bool insertAudio(int64_t frame, Audio audio, std::string* error = nullptr);
    bool replaceSelection(Audio audio, std::string* error = nullptr);
    bool resample(int sampleRate, std::string* error = nullptr);

    bool hasClipboard() const { return !m_clipboard.empty(); }
    const Audio& clipboard() const { return m_clipboard; }
    void clearClipboard();

    const std::vector<Marker>& markers() const { return m_markers; }
    bool addMarker(int64_t frame, std::string name = {});
    bool removeMarker(size_t index);
    void clearMarkers();

    const std::vector<Slice>& slices() const { return m_slices; }
    void setSlices(std::vector<Slice> slices);
    void clearSlices();

private:
    bool rebuild();
    bool validateRange(const Operation& operation, std::string* error) const;
    bool applyProcess(const Operation& operation, Audio& working,
                      std::string* error) const;
    bool applyOperation(const Operation& operation, Audio& working,
                        Selection& after, std::string* error) const;

    struct HistoryEntry {
        Operation operation;
        Selection before;
        Selection after;
    };

    Audio m_source;
    std::vector<HistoryEntry> m_history;
    std::vector<HistoryEntry> m_redo;
    mutable Audio m_rendered;
    mutable bool m_cacheValid = false;
    Selection m_selection;
    Audio m_clipboard;
    std::vector<Marker> m_markers;
    std::vector<Slice> m_slices;
    std::string m_sourcePath;
};

const char* stretchAlgorithmName(StretchAlgorithm algorithm);
bool stretchAlgorithmFromName(const std::string& name,
                              StretchAlgorithm& algorithm);

} // namespace vwave
