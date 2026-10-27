# Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────┐
│ SDL2 editor │ CLI │ Lua 5.4 │ MCP stdio │ YAWN embedding     │
├──────────────────────────────────────────────────────────────┤
│ Session JSON command layer + command helpers/handlers         │
├──────────────────────────────────────────────────────────────┤
│ Timeline: tracks, routing, offsets, mixdown, recording state │
│ Document: immutable source, operation history, undo/redo     │
│ Audio, AudioIO, project format, settings, C ABI              │
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

## Background jobs

`vwave::JobSystem` provides a bounded worker pool with job handles,
cooperative cancellation, and progress reporting. Jobs receive a `JobControl`
reference; long DSP loops can poll `cancelled()` and publish progress. Job
completion uses a future, while UI-facing services will post results back to the
main thread through a dispatcher instead of touching SDL or live documents from
worker threads.

The editor uses the pool for image-to-spectrogram, scope-path, wavetable-note,
overview, spectrogram, phase-spectrogram, beat, and pitch work. Workers return results
through `UiDispatcher`, and only the main thread mutates the timeline or creates
SDL textures. `AnalysisService` owns request tokens and task handles so stale
results are discarded after edits or track changes. Image rendering supports
cooperative progress and cancellation. Snapshot inputs currently copy the
active audio; a copy-on-write/shared audio buffer remains a later optimization.
Document edits themselves still run synchronously.

`Audio` uses copy-on-write shared buffers. Copies and job snapshots share sample
storage until either side writes, then the writer detaches. This keeps async
analysis snapshots cheap even for long tracks without allowing workers to race
with UI edits.

Document revisions let UI caches detect audio changes; waveform peaks are cached
per track/channel/view range instead of being recomputed every frame.

## Editor structure

The desktop editor is being decomposed while preserving behavior:

- `apps/vwave/ui`: theme, formatting, widgets, renderer, and dialog control.
- `apps/vwave/chrome`: menu bar, toolbar, footer, settings, and context menu.
- `apps/vwave/views`: waveform, spectrum, wavetable, mastering, image, scope,
  and sidebar drawing units.
- `apps/vwave/services`: file/path services and future UI-facing job services.

`App.cpp` now acts primarily as the shell and event/action coordinator. The next
extraction stage moves view state and actions into controller classes and then
connects the worker pool through a main-thread dispatcher.

## Audio model

`vwave::Audio` owns planar, non-interleaved 32-bit float channels:

```text
channel 0: f0 f1 f2 ... fN
channel 1: f0 f1 f2 ... fN
```

The layout is contiguous per channel, matching common DAW buffer conventions.
Import/export converts to interleaved data only at the libsndfile boundary.

## Timeline

`vwave::Timeline` is split into track management, stereo conversion, mixdown,
and project IO units. Each track owns a
`vwave::Document` plus a name, timeline offset, gain, pan, mute, solo, arm, and
empty state. Timeline selection is expressed in project frames and translated
to local track frames by subtracting each track's offset. Editing, analysis,
spectral processing, and wavetable extraction operate on the active track.
Playback mixes all audible tracks live. `Timeline::mixdown()` renders a stereo
mix for export, applying mute/solo, gain, pan, offsets, and per-track sample-rate
conversion.

Track-level operations can create derived tracks. Stereo split creates
hard-panned mono tracks and compensates gain for the original stereo balance;
the source is muted or removed. Mono combine creates a stereo track from two
mono sources, applies source gain, preserves track offsets, and pads the shorter
channel at the front, back, or both ends. `Timeline` owns these structural track
changes; they are not part of `Document` history.

Time and pitch processing support two targets: in-place splicing (preserving the
unselected prefix/suffix) or rendering the processed selection to a new track at
its timeline position. Whole-track stretch always edits the active track in
place.

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

Operation construction and serialization live in `Operation.cpp`; shared
document JSON/selection helpers live in `DocumentInternal.h`.

| Class | Operations |
| --- | --- |
| Process | gain, normalize, fades, reverse, invert, silence, DC removal, stretch, pitch, spectral, convolution |
| Timeline | delete, insert, replace range, move range, repeat range, crop |
| Arrangement | set clips, flatten clips |
| Format | resample, replace all |

Move, copy, and repeat are deterministic operations stored by range and
destination/count, so project history remains compact. Repeat is a ripple
insert. Whole-track stretch is a normal time-stretch process over the full
document range.

This design makes undo exact, keeps the original file reference intact, and
allows project files to preserve editing history. It is intentionally
operation-replay rather than copy-on-write snapshots. Very long histories will
eventually benefit from checkpoints; the current implementation prioritises
correctness and simple project serialization.

## Clip arrangement

`ClipArrangement` owns clip validation, rendering, source-range operations, and
clip gain/fades. `Document` delegates clip edits to it while retaining undo
history and project serialization.

A document can own a clip arrangement in addition to its base audio:

```text
base audio = source + non-clip document history
arrangement = ordered clips over base ranges
audio() = rendered arrangement (or base audio when no clips)
```

`Document::Clip` stores a source range, local timeline start, stretch ratio,
gain, fades, and name. Rendering slices the base audio, time-stretches when
needed with WSOLA, applies gain/fades, and mixes clips into the output. Gaps are
silence; overlapping clips are summed.

Clip operations are represented by `SetClips` operations whose payload is the
target clip list. `FlattenClips` renders the arrangement into the base audio and
clears the list. Both replay deterministically, so clip edits share the normal
document undo/redo stack and are stored in the same project history. Destructive
sample operations are rejected while clips exist; flatten first. This keeps
clip source ranges stable and avoids ambiguous edits across the arrangement.

Document-level audio import/export and project JSON persistence live in
`DocumentIO.cpp`, operation execution in `DocumentExecution.cpp`, and
history replay in `DocumentHistory.cpp`; shared JSON/selection helpers live in
`DocumentInternal.h`.

## Persistence

Timeline projects are versioned JSON. Each non-empty track nests its
single-document JSON, including base64 source/operation payloads, history, and
metadata. Track routing state, offsets, active track, and timeline selection are
stored at the timeline level. Legacy single-document projects load as one track.

Application settings (audio device names and theme) are stored separately in
`$XDG_CONFIG_HOME/vwave/settings.json` or `~/.config/vwave/settings.json`.

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

`computePhaseSpectrogram()` adds principal phase, phase-difference
instantaneous frequency, and a magnitude/stability confidence to a bounded
STFT cache. It analyzes at a phase-safe hop and strides stored display frames
to cap memory. The Spectrum 3D view projects this cache as a CPU waterfall:
log-frequency across X, time as depth, level as height, phase as hue, and
confidence as alpha. Hovering reads the nearest bin's level, phase, confidence,
and instantaneous frequency. It is analysis-only and does not alter spectral
editing selection behavior.

### Image synthesis

`loadImageFile()` decodes images through the vendored `stb_image` into an RGBA
pixel buffer. `imageToSpectrogram()` samples luminance/alpha into an STFT
magnitude map, maps image rows to linear or logarithmic frequency and columns to
direct/mirrored/looped time, then reconstructs audio with Griffin–Lim,
momentum/phase-locked variants, random phase, or hue-derived phase. Hann analysis
and synthesis windows are overlap-added and normalized. The core stays UI-free;
image decoding is an independent core module, not SDL code.

### Oscilloscope synthesis

`ScopePath` stores normalized XY points and optional closure. Render traverses
the path by arc length at a configurable scan rate, producing X/Y as stereo
channels. Direction supports forward, reverse, and ping-pong. Raw mode reads the
path directly; band-limited mode oversamples, applies a two-pole Butterworth
low-pass, then decimates. Optional smoothing and peak normalization are applied
after synthesis. Paths persist as versioned `.vscope` JSON.

### Wavetables

Extraction selects evenly spaced frames, estimates cycle length from YIN pitch
or from the requested segmentation, resamples each cycle to a constant frame
size, optionally rotates to the first upward zero crossing, removes DC, and
normalizes. Versioned `.vwt` v2 files store frame data, keyframe positions,
root note, morph/interpolation mode, and quality/artefact metadata. WAV export
writes concatenated mono frames plus a metadata sidecar.

Ordered morphing interpolates keyframes at explicit positions. Free blending
uses a weight per frame. Modes are linear, equal-power, phase-aligned, spectral
(complex spectral interpolation), and harmonic (magnitude interpolation with a
stable phase source). Phase alignment is non-destructive for the phase-aligned
morph mode; explicit alignment, band limiting, quantization, and phase
randomization are separate editing operations. Raw, unnormalized tables load
without forced correction.

### Track recipes

`TimelineTrack` optionally stores a versioned JSON recipe. Image, oscilloscope,
and synth-note generators attach recipes when they create a track. The project
serializer stores them per track. `renderTrackRecipe()` recreates audio from the
recipe, and `regenerate_track` replaces the track source while preserving its
timeline offset. Recipes are metadata; they are outside the per-document undo
history.

### Mastering analysis

The mastering analyzer aligns timeline tracks by offset and runs a
Hann-windowed STFT over the project length. Log-spaced bands report combined
and per-stem power, cross-stem complex coherence, masking density, and
profile-specific gain suggestions. Band power is computed per channel; phase
coherence uses a mono downmix. Profiles control masking strength, maximum
cut/boost, and optional gap filling.

`renderMastering()` applies each stem's static band-gain curve with a
Hann-windowed STFT mask and overlap-add, then sums the processed stems with
their original offsets/gain/pan to check a common mix ceiling. The 4x cubic
true-peak estimate is used; if the mix exceeds the ceiling, one common gain is
applied to all processed stems and the mix. Derived stems can be written as WAVs
or added back to the timeline; source tracks are never replaced. Mix metrics come from the current audible
mixdown (including faders/pan); spectral suggestions use raw stem audio before
track faders. The analyzer never mutates tracks. True peak is estimated with
4× cubic interpolation; loudness is RMS dBFS rather than LUFS.

### Synthesis

A wavetable oscillator with ADSR envelope, multiple detuned unison voices,
stereo panning, MIDI-note frequency mapping, and 1x-8x internal oversampling.
Phase-axis interpolation can be nearest, linear, or cubic. Rendering is offline
and deterministic. YAWN's Wavetable Synth imports `.vwt` files into its Custom
table slot and resamples frames to its 256-sample engine size.

### Capture

Exponential sine sweeps include short fades. The deconvolver performs
regularized frequency-domain division against the sweep, optionally aligns the
peak to a pre-delay, and normalizes the result. Capture manifests use a
versioned JSON format and identify linear IR captures explicitly, leaving a
stable place for future neural-model metadata and runtimes.

## Frontends

### Session command layer

All automation uses `vwave::Session::execute(command, parameters)`. The session
is timeline-backed: document commands target the active track, while track
commands add/remove/select tracks, change routing/offsets, and render mixdowns.
Time parameters are seconds relative to the active track for document
operations. The CLI, Lua, and MCP layers are thin adapters around this API.
Errors are returned as strings; failed commands must not mutate the document or
history.

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

The SDL2 editor holds the canonical `Timeline` on the UI thread. It renders
track headers and stacked waveform lanes, a log-frequency spectrogram for the
active track, and wavetable frames. Track headers expose mute, solo, record arm,
volume, pan, and removal. The inspector sidebar uses draggable parameter
sliders with exact value readouts and cycling algorithm/type selectors; the
sidebar scrolls with the wheel for long tool sets. Menus cover file, editing,
track, processing, view, and settings actions.

Time zoom is shared by waveform and spectrum views; frequency zoom is
independent for the spectrum. Playback streams directly from immutable track
buffers with live routing and on-the-fly linear rate conversion, so pressing
Play does not render or copy the project first. The playback callback supports
a loop range and wraps sample-accurately at the loop end. Click-to-seek is a GUI
concern; the playhead, selection, and loop region remain independent. All edits
stop playback and invalidate analysis/spectrogram caches.

### Recording and settings

Recording uses a separate SDL capture device selected in Settings. The capture
callback writes interleaved floats into a lock-free SPSC ring buffer; the UI
drains it into a staging buffer. On stop, the take fills an empty armed track or
creates a new take track at the playhead. Audio input/output device choices and
the dark/light palette are applied immediately and persisted as versioned JSON.

## Threading

- The timeline, documents, scripting, and MCP handling are control-thread objects.
- The SDL playback callback reads immutable track buffers, live mix parameters,
  and atomics for position/playing state.
- The GUI swaps the playback track list or rebuilds live routing under the SDL
  device lock.
- The SDL capture callback only pushes samples into a lock-free ring buffer.
- There are no allocations, locks, or file operations in the audio callbacks.

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
- phase STFT frequency/confidence, frame caps, and silence handling;
- image decoding and image-to-spectrogram duration, mapping, phase, alpha, and
  invalid-input handling;
- oscilloscope path generation, arc-length sampling, direction, normalization,
  band-limited rendering, and path persistence;
- wavetable extraction, interpolation, persistence, synthesis;
- mastering stem masking, profiles, phase coherence, gain limits, reports, and
  offline processed-stem rendering;
- sweep/deconvolution reciprocity, normalization, capture manifests;
- timeline track controls, offsets, selection scope/translation, per-track crop
  and shift, mixdown, project persistence (including empty/legacy tracks), and
  track recipe round-trips;
- settings persistence and safe defaults;
- ring-buffer wrap/full/discard behavior;
- parameter-slider linear/log/stepped mapping and spectral zoom;
- timeline-backed Session track commands, mixdown export, Lua and MCP track
  automation;
- UI viewport math, C ABI, and a headless editor smoke test.

## Known limitations

- The full history is replayed after undo; checkpointing is future work.
- Track addition/removal/routing is not part of track-document undo history.
- Spectral operations process the selected region in memory.
- Real-time spectral monitoring is not implemented.
- The 3D spectrum is a CPU overview with capped frames/bins and no surface
  selection.
- Image-to-spectrogram stereo output duplicates mono; hue-to-pan, image
  sequences/collages, and regeneration recipes are future work.
- Oscilloscope polyline editing is click-only, and generated tracks do not yet
  embed path recipes.
- Mastering processing is static and offline; time-varying dynamic EQ and
  real-time automation are future work. True peak is an interpolation estimate
  and loudness is RMS dBFS, not LUFS.
- The standalone UI is stereo-focused; the core is channel-agnostic.
- Recording is one target at a time; punch-in, loop recording, and comping are
  future work.
- Neural capture is not implemented; linear IR capture and the manifest format
  are the current capture path.
- Plugin hosting is not part of VWaveEditor.
