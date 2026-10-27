#include "vwave/Session.h"

#include "vwave/Analysis.h"
#include "vwave/Capture.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Mastering.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Processing.h"
#include "vwave/Recipe.h"
#include "vwave/Spectral.h"
#include "vwave/Synth.h"
#include "vwave/Wavetable.h"

#include "session/CommandHelpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace vwave {

using namespace session;

Session::Session(Document document) {
    if (!document.source().empty()) {
        std::string name = "Track 1";
        if (!document.sourcePath().empty())
            name = std::filesystem::path(document.sourcePath())
                       .stem()
                       .string();
        m_timeline.addTrack(name, std::move(document), 0, nullptr);
    }
}

Document& Session::document() {
    const int index = m_timeline.activeTrack();
    if (index < 0) return m_emptyDocument;
    return m_timeline.track(index).document;
}

const Document& Session::document() const {
    const int index = m_timeline.activeTrack();
    if (index < 0) return m_emptyDocument;
    return m_timeline.track(index).document;
}

void Session::syncSelectionFromActive() {
    if (m_timeline.empty()) return;
    const auto& track = m_timeline.track(m_timeline.activeTrack());
    const auto& selection = track.document.selection();
    if (selection.active && selection.end > selection.start) {
        m_timeline.setSelection(selection.start + track.startFrame,
                                selection.end + track.startFrame);
    } else {
        m_timeline.clearSelection();
    }
}

bool Session::loadAudio(const std::string& path, std::string* error) {
    if (error) error->clear();
    Audio audio;
    if (!readAudio(path, audio, error)) return false;
    if (audio.empty()) {
        setError(error, "audio file is empty");
        return false;
    }
    Timeline timeline;
    std::string name = std::filesystem::path(path).stem().string();
    if (name.empty()) name = "Track 1";
    if (!timeline.addAudioTrack(name, audio, 0, error)) return false;
    timeline.track(0).document.setSourcePath(path);
    m_timeline = std::move(timeline);
    return true;
}

bool Session::saveAudio(const std::string& path,
                        const AudioExportOptions& options,
                        std::string* error) {
    if (m_timeline.empty()) {
        setError(error, "no active track");
        return false;
    }
    return document().saveAudio(path, options, error);
}

bool Session::saveMixdown(const std::string& path,
                          const AudioExportOptions& options,
                          std::string* error) {
    if (error) error->clear();
    const Audio mix = m_timeline.mixdown();
    if (mix.empty()) {
        setError(error, "project is empty");
        return false;
    }
    return writeAudio(path, mix, options, error);
}

bool Session::loadProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    return m_timeline.loadProject(path, error);
}

bool Session::saveProject(const std::string& path, std::string* error) {
    if (error) error->clear();
    syncSelectionFromActive();
    return m_timeline.saveProject(path, error);
}

nlohmann::json Session::tracksInfo() const {
    nlohmann::json tracks = nlohmann::json::array();
    for (int i = 0; i < m_timeline.trackCount(); ++i) {
        const auto& track = m_timeline.track(i);
        const int rate = m_timeline.sampleRate();
        const int64_t frames = track.document.numFrames();
        nlohmann::json clips = nlohmann::json::array();
        for (const auto& clip : track.document.clips())
            clips.push_back(clipToJson(clip));
        tracks.push_back(
            {{"index", i},
             {"name", track.name},
             {"active", i == m_timeline.activeTrack()},
             {"start_frame", track.startFrame},
             {"start_seconds",
              rate > 0 ? static_cast<double>(track.startFrame) / rate : 0.0},
             {"frames", frames},
             {"duration",
              rate > 0 ? static_cast<double>(frames) / rate : 0.0},
             {"sample_rate", track.document.sampleRate()},
             {"channels", track.document.numChannels()},
             {"gain_db", track.gainDb},
             {"pan", track.pan},
             {"muted", track.muted},
             {"solo", track.solo},
             {"armed", track.armed},
             {"empty", track.empty},
             {"clip_count", track.document.clips().size()},
             {"clips", std::move(clips)},
             {"source_path", track.document.sourcePath()}});
    }
    return tracks;
}

nlohmann::json Session::info() {
    syncSelectionFromActive();
    nlohmann::json result;
    const Document& doc = document();
    const bool hasTrack = !m_timeline.empty();
    result["ok"] = true;
    result["source_path"] = doc.sourcePath();
    result["sample_rate"] = doc.sampleRate();
    result["channels"] = doc.numChannels();
    result["frames"] = doc.numFrames();
    result["duration"] = doc.duration();
    result["source_frames"] = doc.source().numFrames();
    result["undo_count"] = doc.undoCount();
    result["redo_count"] = doc.redoCount();
    result["undo_label"] = doc.undoLabel();
    result["redo_label"] = doc.redoLabel();
    result["clipboard_frames"] = doc.clipboard().numFrames();
    result["track_count"] = m_timeline.trackCount();
    result["active_track"] = m_timeline.activeTrack();
    result["project_sample_rate"] = m_timeline.sampleRate();
    result["project_frames"] = m_timeline.lengthFrames();
    result["project_duration"] = m_timeline.duration();
    result["selection"] = {
        {"start", doc.selection().start},
        {"end", doc.selection().end},
        {"active", doc.selection().active},
        {"start_seconds", doc.sampleRate() > 0
                              ? static_cast<double>(doc.selection().start) /
                                    doc.sampleRate()
                              : 0.0},
        {"end_seconds", doc.sampleRate() > 0
                            ? static_cast<double>(doc.selection().end) /
                                  doc.sampleRate()
                            : 0.0}};
    result["timeline_selection"] = {
        {"start", m_timeline.selection().start},
        {"end", m_timeline.selection().end},
        {"active", m_timeline.selection().active},
        {"start_seconds",
         m_timeline.sampleRate() > 0
             ? static_cast<double>(m_timeline.selection().start) /
                   m_timeline.sampleRate()
             : 0.0},
        {"end_seconds",
         m_timeline.sampleRate() > 0
             ? static_cast<double>(m_timeline.selection().end) /
                   m_timeline.sampleRate()
             : 0.0}};
    result["markers"] = nlohmann::json::array();
    for (const auto& marker : doc.markers())
        result["markers"].push_back(markerToJson(marker, doc.sampleRate()));
    result["slices"] = nlohmann::json::array();
    for (const auto& slice : doc.slices())
        result["slices"].push_back(sliceToJson(slice, doc.sampleRate()));
    result["tracks"] = tracksInfo();
    result["active_track_name"] =
        hasTrack ? m_timeline.track(m_timeline.activeTrack()).name
                 : std::string{};
    return result;
}

nlohmann::json Session::execute(const std::string& command,
                                const nlohmann::json& parameters,
                                std::string* error) {
    if (error) error->clear();
    try {
        if (!parameters.is_object() && !parameters.is_null()) {
            setError(error, "parameters must be an object");
            return nullptr;
        }
        const nlohmann::json params =
            parameters.is_null() ? nlohmann::json::object() : parameters;

        if (command == "info") return info();

        if (command == "tracks" || command == "list_tracks" ||
            command == "track_info") {
            return {{"ok", true},
                    {"track_count", m_timeline.trackCount()},
                    {"tracks", tracksInfo()},
                    {"active_track", m_timeline.activeTrack()}};
        }
        if (command == "oscilloscope_render" ||
            command == "image_spectrogram")
            return executeImageScopeCommands(command, params, error);
        if (command == "mastering_analysis" ||
            command == "mastering_render")
            return executeMasteringCommands(command, params, error);
        if (command == "regenerate_track")
            return executeRecipeCommands(command, params, error);

        if (command == "add_track" || command == "add_empty_track" ||
            command == "remove_track" || command == "select_track" ||
            command == "track_set" || command == "selection_scope" ||
            command == "crop_track" || command == "shift_track" ||
            command == "stretch_track" || command == "split_stereo_track" ||
            command == "combine_mono_tracks")
            return executeTrackCommands(command, params, error);

        if (command == "save_mixdown" || command == "load_audio" ||
            command == "save_audio" || command == "load_project" ||
            command == "save_project")
            return executeIoCommands(command, params, error);

        if (command == "select" || command == "select_all" ||
            command == "clear_selection" || command == "undo" ||
            command == "redo" || command == "copy" || command == "cut" ||
            command == "delete" || command == "paste" ||
            command == "crop" || command == "move_selection" ||
            command == "repeat_selection" || command == "snap_selection" ||
            command == "add_marker" || command == "remove_marker" ||
            command == "clear_markers")
            return executeEditCommands(command, params, error);

        if (command == "stretch_track" || command == "split_stereo_track" ||
            command == "combine_mono_tracks")
            return executeTrackCommands(command, params, error);

        if (command == "make_clips" || command == "flatten_clips" ||
            command == "split_clip" || command == "delete_clip" ||
            command == "move_clip" || command == "stretch_clip" ||
            command == "repeat_clip" || command == "set_clip_gain" ||
            command == "set_clip_fades")
            return executeClipCommands(command, params, error);

        if (command == "gain" || command == "normalize" ||
            command == "fade_in" || command == "fade_out" ||
            command == "reverse" || command == "invert" ||
            command == "silence" || command == "dc_remove" ||
            command == "insert_silence" || command == "insert_audio" ||
            command == "time_stretch" || command == "paulstretch" ||
            command == "pitch_shift" || command == "resample" ||
            command == "spectral_gain" || command == "spectral_filter" ||
            command == "spectral_gate" || command == "spectral_blur")
            return executeEditCommands(command, params, error);

        if (command == "analyze_beats" || command == "detect_pitch" ||
            command == "slices" || command == "export_slices")
            return executeAnalysisCommands(command, params, error);

        if (command == "wavetable" || command == "harmonics_wavetable" ||
            command == "wavetable_design" || command == "synth_note")
            return executeWavetableCommands(command, params, error);
        if (command == "generate_sweep" || command == "deconvolve")
            return executeCaptureCommands(command, params, error);

        setError(error, "unknown command: " + command);
        return nullptr;
    } catch (const std::exception& e) {
        setError(error, std::string("command failed: ") + e.what());
        return nullptr;
    }
}

std::vector<std::string> Session::commandNames() {
    return {
        "info", "tracks", "list_tracks", "track_info", "mastering_analysis",
        "image_spectrogram", "oscilloscope_render", "mastering_render",
        "regenerate_track", "add_track",
        "add_empty_track", "remove_track", "select_track", "track_set",
        "selection_scope", "crop_track", "shift_track",
        "split_stereo_track", "combine_mono_tracks", "stretch_track",
        "make_clips", "flatten_clips", "split_clip", "delete_clip",
        "move_clip", "stretch_clip", "repeat_clip", "set_clip_gain",
        "set_clip_fades",
        "save_mixdown", "load_audio", "save_audio", "load_project",
        "save_project", "select", "select_all", "clear_selection", "undo",
        "redo",
        "copy", "cut", "delete", "move_selection", "repeat_selection",
        "snap_selection", "paste", "crop", "gain", "normalize",
        "fade_in", "fade_out", "reverse", "invert", "silence", "dc_remove",
        "insert_silence", "insert_audio", "time_stretch", "paulstretch",
        "pitch_shift", "resample", "spectral_gain", "spectral_filter",
        "spectral_gate", "spectral_blur", "analyze_beats", "detect_pitch",
        "slices", "export_slices", "wavetable", "harmonics_wavetable",
        "wavetable_design", "synth_note", "generate_sweep", "deconvolve",
        "add_marker",
        "remove_marker", "clear_markers"};
}

} // namespace vwave
