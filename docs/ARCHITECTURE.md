# Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────┐
│ SDL2 editor │ CLI │ Lua 5.4 │ MCP stdio │ YAWN embedding     │
├──────────────────────────────────────────────────────────────┤
│ Session JSON command layer                                   │
├──────────────────────────────────────────────────────────────┤
│ Document: immutable source, operation history, undo/redo     │
│ Audio, AudioIO, project format, C ABI                        │
├──────────────────────────────────────────────────────────────┤
│ DSP        Analysis     Spectral     Wavetable     Capture   │
│ FFT        Onsets        STFT         Extract      Sweep     │
│ Resample   Tempo         Gain/filter  Harmonics    Deconvolve│
│ WSOLA/PV   Pitch         Gate/blur    Render       Manifest  │
│ PaulStretch Slicing      Spectrogram                          │
└──────────────────────────────────────────────────────────────┘
```

`vwave_core` has no window-system dependency. SDL exists only in the
`vwave_editor` executable. Scripting and MCP live in the core because they
operate on the same command layer as the GUI.

## Audio model

`vwave::Audio` owns planar, non-interleaved 32-bit float channels:

```text
channel 0: f0 f1 f2 ... fN
channel 1: f0 f1 f2 ... fN
```

The layout is contiguous per channel, matching common DAW buffer conventions.
Import/export converts to interleaved data only at the libsndfile boundary.

## Non-destructive document

A document stores:

- immutable source `Audio`;
- ordered `HistoryEntry` records: operation, selection before, selection after;
- redo stack;
- clipboard;
- markers and slices;
- source path and project path;
- a rendered audio cache.

Editing applies an operation to the current rendered buffer, records the
result, and keeps the source untouched. Undo removes the last history entry
and replays the remaining operations from the source. Redo applies the
operation again and restores its post-operation selection.

Supported operation classes:

| Class | Operations |
| --- | --- |
| Process | gain, normalize, fades, reverse, invert, silence, DC removal, stretch, pitch, spectral, convolution |
| Timeline | delete, insert, replace range, crop |
| Format | resample, replace all |

This design makes undo exact, keeps the original file reference intact, and
allows project files to preserve editing history. It is intentionally
operation-replay rather than copy-on-write snapshots. Very long histories will
eventually benefit from checkpoints; the current implementation prioritises
correctness and simple project serialization.

## Persistence

Projects are JSON with a versioned envelope. The source and binary operation
payloads are base64-encoded float data, so a project is self-contained. Each
history entry stores its operation, pre-selection, and post-selection.
Markers, slices, source path, and the current selection are stored separately.

Audio export flattens the rendered buffer through libsndfile. WAV, AIFF, FLAC,
and Ogg Vorbis are supported; float/integer bit depths are selected through
`AudioExportOptions`.

## DSP

### FFT

A radix-2 Cooley-Tukey FFT with precomputed bit reversal and twiddle factors.
Inverse transforms use conjugation and `1/N` scaling. FFT sizes are validated
and rounded to powers of two at higher-level entry points.

### Resampling

A windowed-sinc resampler with a Blackman-style cosine window, configurable
zero crossings, low-pass rolloff for downsampling, and per-output weight
normalization. It preserves DC and amplitude at band edges.

### Time stretching

- **WSOLA** uses overlapping Hann windows, cross-correlation search around the
  nominal analysis position, and normalized overlap-add. It preserves pitch
  and is transient-friendly.
- **Phase vocoder** propagates phase using instantaneous-frequency estimates
  and overlap-add with window-power normalization.
- **PaulStretch** uses randomized phase initialization, phase propagation, and
  wide windows to produce smeared ambient textures while retaining pitch.

Pitch shifting time-stretches by the pitch ratio and resamples by its inverse,
then fits the result back to the original frame count.

### Analysis

- Onsets use spectral flux plus energy rise, normalized and peak-picked with a
  local adaptive threshold and minimum inter-onset gap.
- Tempo scoring uses inter-onset intervals from strong onsets, a log-tempo
  prior, and a beat grid phased from the first onset.
- Pitch uses YIN difference/CMND analysis with parabolic interpolation.
- Slicing supports transient, beat-grid, and marker boundaries.

### Spectral editing

A Hann-windowed STFT with 75% overlap. Analysis and synthesis windows are both
applied and the overlap-add is normalized by the window-power sum. Spectral
gain masks use cosine feathering; filters use Butterworth magnitude responses;
gating compares each bin to a per-frame threshold; blur smooths magnitudes over
time and frequency. Time-frequency regions are converted to document ranges
before applying edits.

### Wavetables

Extraction selects evenly spaced frames, estimates cycle length from YIN pitch
or from the requested segmentation, resamples each cycle to a constant frame
size, optionally rotates to the first upward zero crossing, removes DC, and
normalizes. Wavetable `.vwt` files store frame metadata and base64 float data.
WAV export writes concatenated mono frames plus a metadata sidecar.

### Synthesis

A wavetable oscillator with linear phase/frame interpolation, ADSR envelope,
multiple detuned unison voices, stereo panning, and MIDI-note frequency
mapping. Rendering is offline and deterministic.

### Capture

Exponential sine sweeps include short fades. The deconvolver performs
regularized frequency-domain division against the sweep, optionally aligns the
peak to a pre-delay, and normalizes the result. Capture manifests use a
versioned JSON format and identify linear IR captures explicitly, leaving a
stable place for future neural-model metadata and runtimes.

## Frontends

### Session command layer

All automation uses `vwave::Session::execute(command, parameters)`. Commands
take seconds for user-facing time parameters and return JSON. The CLI, Lua, and
MCP layers are thin adapters around this API. Errors are returned as strings;
failed commands must not mutate the document or history.

### Lua

`ScriptEngine` owns a Lua 5.4 state and registers a `vwave` table. Scripts can
invoke high-level commands or map samples through a callback. The callback is
copied into a new document operation only after the entire mapping succeeds, so
a runtime error leaves the document untouched.

### MCP

`McpServer` implements newline-delimited JSON-RPC 2.0 with `initialize`,
`tools/list`, `tools/call`, resources, and ping. Tool results include text JSON
and, where possible, structured content. The generic `vwave_command` tool
exposes the entire command surface; dedicated tools provide clearer schemas for
common workflows. The server is local stdio only and executes with the
permissions of the user running it.

### Standalone editor

The SDL2 editor holds the canonical `Document` on the UI thread. It renders
waveform peaks, a log-frequency spectrogram, and wavetable frames. Playback
copies the current selection into an audio-device-rate stereo buffer, so later
edits cannot invalidate the audio callback's data. All edits stop playback and
invalidate analysis/spectrogram caches.

## Threading

- The document, scripting, and MCP handling are control-thread objects.
- The SDL audio callback only reads an immutable playback buffer and atomics
  for position/playing state.
- The GUI swaps the playback buffer while the SDL device is locked.
- There are no allocations, locks, or file operations in the audio callback.

Embedders that need to feed a DAW audio thread should snapshot the rendered
buffer after edits and publish it through their own lock-free mechanism.

## Testing

The suite covers:

- audio construction, mapping, slices, peaks, WAV round-trips;
- document gain/delete/replace/crop, undo/redo, clipboard, project round-trip;
- FFT identity, resampling frequency/duration, WSOLA/phase-vocoder/PaulStretch
  length and pitch, pitch shift, convolution;
- onset detection, tempo including accented subdivisions, pitch detection,
  slicing, slice export;
- spectrogram dimensions, STFT gain/filter/gate/range isolation;
- wavetable extraction, interpolation, persistence, synthesis;
- sweep/deconvolution reciprocity, normalization, capture manifests;
- Session, Lua, MCP, UI viewport math, C ABI, and a headless editor smoke test.

## Known limitations

- The full history is replayed after undo; checkpointing is future work.
- Spectral operations process the selected region in memory.
- Real-time spectral monitoring is not implemented.
- The standalone UI is stereo-focused; the core is channel-agnostic.
- Neural capture is not implemented; linear IR capture and the manifest format
  are the current capture path.
- Plugin hosting is not part of VWaveEditor.
