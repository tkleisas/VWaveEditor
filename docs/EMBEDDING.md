# Embedding in YAWN

VWaveEditor is designed as two pieces:

1. `vwave_core` — UI-free C++17 timeline/document editing, DSP, analysis,
   recording buffers, settings, and automation.
2. `vwave_editor` — a standalone SDL2 frontend used as a reference client.

YAWN should integrate the core. Do not try to parent the SDL2 editor window
inside YAWN's SDL3/OpenGL window.

## CMake

```cmake
set(VWAVE_BUILD_GUI OFF CACHE BOOL "" FORCE)
set(VWAVE_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(VWAVE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../VWaveEditor VWaveEditor)
target_link_libraries(yawn_core PRIVATE vwave::core)
```

`vwave_core` links libsndfile and (by default) vendored Lua. Disable Lua with
`-DVWAVE_BUILD_LUA=OFF` if the host wants to manage its own Lua state.

## Multi-track timeline

Use `vwave::Timeline` when the host needs layered tracks:

```cpp
vwave::Timeline timeline;
timeline.addAudioTrack("Guitar", guitarAudio, 0, &error);
timeline.addAudioTrack("Synth", synthAudio, 0, &error);
timeline.setTrackGainDb(0, -3.0);
timeline.setTrackPan(1, 0.2);
timeline.setTrackMuted(1, false);
const vwave::Audio mix = timeline.mixdown();
```

A `Timeline` stores an independent non-destructive `Document` per track plus
mute/solo/arm/gain/pan/offset state. Track edits use local frames; timeline
selection is translated to each track automatically. Use `Timeline::saveProject`
and `loadProject` for multi-track sessions. The C ABI currently wraps a
timeline-backed `Session`; direct C functions operate on the active track, while
`vw_session_execute` exposes the track commands.

## Snapshotting a YAWN clip

```cpp
#include <vwave/Document.h>
#include <vwave/Audio.h>

std::unique_ptr<vwave::Document> makeEditorDocument(
    const yawn::audio::AudioBuffer& buffer, int sampleRate) {
    vwave::Audio audio(buffer.numChannels(), buffer.numFrames(), sampleRate);
    for (int ch = 0; ch < buffer.numChannels(); ++ch) {
        std::copy_n(buffer.channelData(ch), buffer.numFrames(),
                    audio.channel(ch));
    }
    return std::make_unique<vwave::Document>(
        vwave::Document::fromAudio(std::move(audio)));
}
```

After an edit, publish a snapshot to the audio thread:

```cpp
const vwave::Audio& rendered = document.audio();
auto snapshot = std::make_shared<const vwave::Audio>(rendered);
publishToAudioThread(std::move(snapshot));
```

`Document::audio()` returns a stable reference until the next edit, undo, or
redo. Never hand that reference directly to a real-time thread that can race
with UI edits.

## Applying edits

Use high-level methods for simple operations:

```cpp
document.setSelection(48000, 96000);
document.gainDb(-3.0);
document.normalize(-1.0);
document.reverseSelection();
document.fadeOut();
document.undo();
```

Use the operation API for timeline changes and automation:

```cpp
vwave::Operation op = vwave::Operation::replaceRange(
    {48000, 96000}, replacementAudio);
std::string error;
if (!document.apply(op, &error)) {
    showError(error);
}
```

`vwave::Session` exposes the same surface as JSON for an automation panel or
agent bridge:

```cpp
vwave::Session session(std::move(document));
std::string error;
auto result = session.execute(
    "spectral_gain",
    {{"low_hz", 200.0}, {"high_hz", 2000.0}, {"db", -6.0}},
    &error);
```

`Session` does not permit copying an existing `Document` because the document
owns its history.

## UI integration with fw2

Use core types directly from a YAWN panel:

- `vwave::computePeaks(audio, channel, range, pixelWidth)` for waveform bars.
- `vwave::computeSpectrogram(audio, fftSize, hop)` for a spectrogram texture;
  `vwave::computePhaseSpectrogram(audio, fftSize, maxFrames)` for phase,
  instantaneous-frequency, and confidence data.
- `vwave::analyzeBeats(audio)` for tempo/onsets/beats.
- `vwave::detectPitch(audio, range)` for the pitch readout.
- `vwave::WaveformViewport` and `vwave::SpectralViewport` for coordinate
  mapping; both are headless and unit tested.
- `vwave::Wavetable::sampleMorph(position, phase)` or
  `sampleBlend(weights, phase)` for ordered/free wavetable playback;
  `morphedFrame(position)` for display.
- `vwave::Wavetable::loadAuto(path, table)` for `.vwt` or WAV+sidecar import.
- `vwave::analyzeMastering(timeline, options)` for analysis-only stem
  masking/phase/gain reports.
- `vwave::imageToSpectrogram(image, options)` for UI-free image synthesis.

The standalone editor demonstrates the intended interaction model: selection
drag, wheel zoom, beat/slice/marker overlays, time-frequency region selection,
wavetable frame drawing, and transport.

## C ABI

Hosts that do not want to expose C++ ABI details can use
`include/vwave/vwave_c.h`:

```c
VWSession* session = vw_session_create();
char error[512];
if (vw_session_load_audio(session, "take.wav", error, sizeof(error)) != 0) {
    /* report error */
}

VWDocumentInfo info;
vw_session_info(session, &info);
vw_session_select(session, 48000, 96000);
vw_session_normalize(session, -1.0, error, sizeof(error));

const float* left = vw_session_channel(session, 0);
/* use left while no edit is in flight */
vw_session_destroy(session);
```

The C handle wraps a timeline-backed `Session`. Direct functions operate on the
active track; `vw_session_execute` exposes all track-aware commands. It supports
file/project I/O, rendered interleaved copies, undo/redo, selection, common
edits, and JSON command dispatch. Errors are written into caller-provided
buffers. The ABI version is reported through `VWDocumentInfo::abi_version`.

## Threading contract

- Create and edit documents on one control thread.
- Analyse on a worker thread only if the document is not being edited; copy the
  `Audio` first for a stable snapshot.
- Never call `Document::audio()` from a real-time callback.
- Playback buffers are immutable and swapped under the host's own locking
  rules.
- Long spectral/stretch operations are currently synchronous; run them off the
  UI thread in the host and marshal completion back to the editor model.

## Suggested YAWN integration points

- A `DetailPanel` tab for the selected audio clip, backed by a `Document`.
- "Edit in VWave" action that copies the clip into a `Document`, opens an
  editor surface, and writes the rendered result back on commit.
- A browser preview using `computePeaks` and `computeSpectrogram`.
- Session/MCP automation against the active VWave document for agent-assisted
  sound design.
- Clip-level destructive bake only at commit time; keep the VWave project as
  the non-destructive source of truth.

## Licensing

VWaveEditor is MIT. libsndfile is linked dynamically under LGPL; see
`NOTICES.md` and the host's distribution policy.
