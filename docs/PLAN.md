# VWaveEditor development plan

## Product goal

VWaveEditor is an advanced, non-destructive wave editor that runs as a standalone application and as an embeddable C++ audio component for YAWN. The first release establishes a trustworthy editing/DSP core and a usable desktop editor; later releases deepen the analysis, spectral, synthesis, capture, and agent workflows.

## Non-negotiable properties

1. Source audio is never modified by an edit.
2. Every edit is undoable and redoable.
3. The editor core is independent of SDL and any other UI toolkit.
4. Every public capability has automated tests.
5. File processing is deterministic for a given input and operation set.
6. Script and MCP access use the same command surface as the desktop UI.
7. Audio-thread code never allocates, locks, or performs file I/O.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│ Standalone SDL2 editor │ CLI │ Lua runner │ MCP stdio server │
│                              │            │                  │
│                         vwave::Session / command API         │
├──────────────────────────────────────────────────────────────┤
│ vwave_core                                                   │
│  Audio + AudioIO      Non-destructive Document/undo          │
│  DSP                  Beat/pitch/onset analysis              │
│  STFT + spectral ops  Wavetable creation/editing             │
│  Synth rendering      IR/model capture                       │
│  Project persistence  Embedding API                          │
└──────────────────────────────────────────────────────────────┘
             │
             └── linked by YAWN, tests, CLI, GUI, Lua, MCP
```

The project is a static library plus thin frontends. Public headers live in
`include/vwave`. YAWN can link `vwave_core`, open a `Document`, execute edits,
and pull the rendered `Audio` without creating a window or an application.

## Non-destructive edit model

A `Document` owns:

- an immutable source `Audio` object;
- an ordered operation history;
- a redo stack;
- a selection, markers, and clipboard;
- a cached rendered result.

Each operation is replayed from the original source when the history changes.
Operations are one of: process a sample range, delete a range, insert audio,
replace a range, crop, resample, or replace the whole document. This keeps the
source intact, makes undo/redo exact, and allows project serialisation while
DSP algorithms evolve.

## Delivery phases

### Phase 0: Contracts and test harness

Exit criteria:

- CMake project builds core, CLI, GUI, and tests.
- Public API headers compile independently.
- Test runner and CTest integration exist.
- `docs/PLAN.md` and `docs/ARCHITECTURE.md` describe the contracts.

### Phase 1: Audio and document core

Tests first:

- construction, planar access, slicing, interleaving, peak/RMS;
- WAV read/write round-trip;
- gain/delete/replace/crop preserve the source;
- undo/redo restore byte-identical rendered output;
- cut/copy/paste and selection clamping;
- project save/load restores history, markers, and selection;
- undo after destructive-looking operations.

Implementation:

- `Audio`, `AudioIO`, `Base64`, `Document`, operation history, clipboard.
- Flattened audio export.

### Phase 2: DSP and transformations

Tests first:

- FFT forward/inverse round-trip;
- resampler length and sine-frequency accuracy;
- WSOLA and phase-vocoder duration guarantees;
- pitch shift returns original duration with measured frequency ratio;
- PaulStretch duration and finite output;
- fades, reverse, normalize, DC removal, and mixdown.

Implementation:

- radix-2 FFT;
- windowed-sinc resampler;
- WSOLA, phase vocoder, PaulStretch;
- pitch shifting via stretch plus resample;
- edit primitives and spectral transforms.

### Phase 3: Analysis, slicing, and beat grid

Tests first:

- known click train yields expected BPM and beat positions;
- onset detector finds impulses;
- pitch detector measures A4/220/880 Hz;
- slice boundaries are ordered, unique, and bounded;
- slice export produces the declared files.

Implementation:

- spectral-flux onsets, adaptive peak picking;
- tempo autocorrelation and beat phase estimation;
- autocorrelation/YIN pitch detection;
- transient, bar, and manual slicing;
- WAV slice export with manifest.

### Phase 4: Spectral editing

Tests first:

- STFT/ISTFT identity within a reconstruction tolerance;
- spectral gain affects the requested band only;
- spectral gate attenuates below-threshold content;
- spectral filter slopes and cutoff behaviour;
- time-frequency region edits leave outside material intact.

Implementation:

- streaming STFT/ISTFT with Hann overlap-add;
- spectral gain, filter, gate, blur, and region erase;
- spectrogram data for rendering.

### Phase 5: Wavetables, synthesis, and capture

Tests first:

- wavetable frame size/count/normalisation invariants;
- cycle extraction from a known periodic signal;
- wavetable WAV/metadata round-trip;
- additive harmonic construction;
- wavetable note rendering has expected duration and pitch;
- sweep length, IR deconvolution peak, and normalisation;
- deterministic project/model metadata.

Implementation:

- wavetable extraction, editing, morphing, and persistence;
- band-limited table oscillator and ADSR rendering;
- exponential sweep generation;
- FFT deconvolution, IR alignment/trim, and linear transfer captures;
- model-capture manifest format with a backend interface for future neural
  capture runtimes.

### Phase 6: Lua and MCP

Tests first:

- Lua can load, edit, analyse, stretch, and save;
- sample callbacks are bounded and type-checked;
- script errors propagate without corrupting the document;
- MCP initialize, tools/list, tools/call, bad-method, and bad-params cases;
- MCP tool calls produce the same state changes as direct API calls.

Implementation:

- Lua 5.4 binding;
- shared `Session` command facade;
- JSON-RPC 2.0 MCP stdio server with tools and document resources;
- CLI command mode with the same operations.

### Phase 7: Standalone editor

Tests first:

- viewport mapping and sample-to-pixel invariants;
- selection/zoom/pan interactions in a headless UI model;
- operation dispatch tests independent of rendering;
- smoke startup test for the SDL application.

Implementation:

- SDL2 window, SDL_Renderer, SDL_ttf;
- waveform view with overview, zoom, selection, beats, slices, playhead;
- spectrogram view with time-frequency selection and edits;
- wavetable view with frame navigation, drawing, extraction, save;
- playback transport;
- analysis and command panels;
- keyboard shortcuts and drag-drop import.

### Phase 8: YAWN integration and hardening

Tests first:

- embed a document in a non-UI test and render a clip;
- stable C ABI smoke test;
- long-file/ranges and malformed-file tests;
- sanitizer run;
- end-to-end CLI script.

Implementation:

- embedding guide and example;
- C ABI facade;
- YAWN clip adapter example;
- performance passes for waveform rendering and spectral operations.

## API shape

```cpp
vwave::Document doc = vwave::Document::fromAudio(audio);
doc.setSelection({start, end});
doc.gainDb(-3.0);
doc.reverseSelection();
doc.normalize(-1.0);
doc.undo();
doc.redo();
doc.deleteSelection();
doc.paste();

const vwave::Audio& rendered = doc.audio();
```

The same operations are exposed as JSON through `vwave::Session`, which is
wrapped by Lua, MCP, and the CLI. The GUI uses `Document` directly.

## Testing strategy

- Every feature starts with a failing test that encodes the observable
  contract.
- Tests use synthetic signals with known answers (sine waves, impulses,
  click trains) so assertions are objective.
- Numeric tests use tolerances appropriate to the algorithm; no exact float
  comparisons except for non-DSP invariants and undo/redo.
- Each phase ends with the full suite, an ASan/UBSan build, and a manual smoke
  test of the standalone application.
- Test data is generated at runtime; no binary fixtures are committed.

## Initial release scope

Included:

- non-destructive editing, undo/redo, project persistence;
- WAV/AIFF/FLAC/OGG import and WAV/FLAC/OGG export through libsndfile;
- trim, cut, copy, paste, insert, delete, crop, gain, normalise, fades,
  reverse, invert, silence, DC removal, resample;
- WSOLA, phase-vocoder, and PaulStretch time manipulation;
- pitch shifting;
- beat, onset, pitch, and slice analysis;
- spectral gain/filter/gate and time-frequency editing;
- wavetable extraction/creation/editing and additive synthesis;
- sweep generation and linear impulse-response capture;
- Lua scripting and MCP automation;
- standalone SDL2 editor;
- embeddable core with a C++ API and C shim.

Deferred:

- neural amp/instrument capture training and inference;
- real-time spectral editing while monitoring;
- VST/AU hosting;
- collaborative editing;
- multichannel beyond stereo in the first UI iteration (core is
  channel-agnostic);
- mobile frontends;
- image-to-spectrogram synthesis;
- oscilloscope-music path generation;
- phase-aware pseudo-3D spectral display;
- multi-wave wavetable morph designer;
- mastering analysis and cross-track dynamic spectral allocation.

## Risks

- Offline DSP can be slow on very long files. Mitigation: chunked processing,
  progress callbacks, and cancellable jobs.
- Spectral editing needs careful overlap-add normalisation to avoid gain
  artefacts. Mitigation: reconstruction and band-isolation tests.
- WSOLA and PaulStretch trade quality for implementation simplicity.
  Mitigation: explicit algorithms and future pluggable backends.
- MCP/Lua access can mutate documents unexpectedly. Mitigation: shared,
  validated command layer, explicit error codes, and bounded batch operations.
- Neural capture is a large separate runtime. Mitigation: keep a stable capture
  manifest/backend interface and ship linear IR capture first.

## Implementation status

As of the v0.0.7 development line:

- Phases 0–8 are implemented and test-green.
- Multi-track Timeline, track controls, multi-track playback/export, recording,
  audio-device/theme settings, menu bar, target-to-new-track processing, and
  time/frequency zoom are implemented.
- Neural capture, real-time spectral monitoring, plugin hosting, and
  multichannel UI remain deferred.
- The automated suite covers audio I/O, non-destructive history, Timeline
  tracks/mixdown/persistence, track-aware Session/Lua/MCP automation, FFT,
  resampling, WSOLA/phase-vocoder/PaulStretch, pitch shift, onsets, tempo,
  pitch, slicing, spectral editing, wavetables, synthesis, sweep deconvolution,
  settings, ring buffers, UI viewport math, C ABI, and a headless desktop smoke
  test.

## Future feature plan (post-v0.0.6, implemented through v0.0.7)

Detailed design notes and options are recorded in
[docs/IDEAS.md](IDEAS.md). All planned phases 9-15 are implemented.

### Phase 9: Wavetable designer and morph lab (implemented)

Implemented: ordered keyframes and free blending; linear, equal-power,
phase-aligned, spectral, and harmonic morph modes; nearest/linear/cubic phase
interpolation; optional phase alignment, band limiting, quantization, phase
randomization, and 1x-8x synth oversampling; `.vwt` v2 persistence with raw
data preserved; WAV + sidecar metadata; designer controls, Lua/MCP/CLI command
surfaces; and YAWN Custom-table import via its own **Load .vwt...** button,
including project/preset persistence.

Original scope:

- Ordered and free-blend multi-keyframe tables.
- Linear, equal-power, phase-aligned, spectral, and harmonic morph modes.
- Explicit quality/artefact controls: phase alignment, normalization, DC,
  aliasing/band-limiting, resampling quality, quantization, phase randomization.
- Designer UI with import/draw/harmonics, keyframe strip, morph automation,
  A/B preview, and v2 `.vwt` persistence.
- A shared, tested wavetable interchange path for YAWN; modify YAWN as needed to
  load custom VWaveEditor wavetables while preserving raw/non-normalized data.
- Tests for endpoints, boundedness, phase alignment, raw aliasing determinism,
  persistence, and VWaveEditor-to-YAWN frame/morph round-trips.

### Phase 10: Mastering analysis (implemented)

Implemented analysis-only mastering for aligned performance stems:

- Log-band STFT stem analysis with masking/overlap, dominance, phase coherence,
  and mix/per-stem levels.
- Profiles: balanced, definition, separation, fullness, loudness, and custom.
- Suggested per-band cuts/boosts with maximum-gain and smoothing constraints.
- RMS/peaks/estimated true peak, crest, and stereo correlation metrics.
- Mastering UI tab, JSON report export, CLI, Lua, MCP, and Session command.
- Synthetic-stem tests for overlap, profile strength, phase cancellation,
  offsets, mute handling, limits, and report persistence.

### Phase 11: Phase-aware pseudo-3D spectrum (implemented)

Implemented:

- `PhaseSpectrogram` with phase, phase-difference instantaneous frequency, and
  magnitude/phase-stability confidence.
- Bounded phase STFT cache with automatic hop sizing for long files.
- Spectrum `View: 2D/3D` CPU waterfall: log-frequency X, time depth, magnitude
  height, phase hue, confidence alpha.
- Hover readout for frequency, level, confidence, phase, and instantaneous Hz.
- Phase data tests for tone tracking, silence confidence, frame caps, and
  dimensions.

Remaining from the original scope: masking/noise-floor overlays and
time-frequency selection inside the 3D surface; an OpenGL renderer if needed.

### Phase 12: Image-to-spectrogram (implemented)

Implemented:

- Vendored image decoding plus a UI-free pixel-buffer/`ImageData` API.
- Luminance/alpha magnitude mapping, threshold/contrast/gamma/invert, crop/flip
  options, linear/log frequency mapping, direct/mirrored/looped time mapping.
- Griffin–Lim, momentum Griffin–Lim, phase-locked initialization, random, and
  hue-derived color phase.
- Image UI tab with decode/preview, parameter controls, and render-to-track.
- CLI, Lua, MCP, and Session automation; mono/stereo output and normalization.
- Tests for PPM decoding, frequency mapping, time maps, color phase, alpha,
  silence, and invalid input.

Remaining from the original scope: image sequences/collages, hue-to-pan or
separate band/track mapping, generated-recipe persistence, in-app audio preview,
and progress/cancel for very long Griffin–Lim jobs.

### Phase 13: Oscilloscope music (implemented)

Implemented:

- XY scope view with generated circle, ellipse, Lissajous, spiral, figure-eight,
  and line paths; hand-drawn polyline editing.
- Arc-length traversal to stereo audio with configurable scan rate and
  forward/reverse/ping-pong direction.
- Raw and band-limited rendering with 1x–8x oversampling, low-pass filtering,
  smoothing, and normalization.
- `.vscope` path save/load, Scope tab, CLI, Lua, MCP, and Session automation.
- Tests for shapes, arc-length sampling, stereo circle output, direction,
  limits, path persistence, and invalid input.

Remaining from the original scope: text/SVG import, multi-pass thickness,
embedded track recipes, and editing existing stereo audio in the XY domain.

### Phase 14: Mastering processing (implemented)

Implemented:

- Static per-band gain curves applied through STFT masks and overlap-add.
- Derived processed stems and a mastered mix; source tracks preserved.
- Optional common ceiling gain using the 4x cubic true-peak estimate.
- Processed stems can be written as WAVs with a JSON manifest or added back to
  the timeline at their original offsets/gain/pan.
- UI render button, CLI `mastering-render`, Lua, MCP, and Session command.
- Tests for processed-stem lengths, determinism, ceiling constraints, output
  files, derived-track insertion, and report generation.

Remaining from the original scope: time-varying dynamic EQ, real-time track
automation, reference matching, and standards-compliant LUFS metering.

### Agreed priorities

The phase numbers below group capability workstreams; the delivery order is:

1. ~~Wavetable designer and morph lab, including YAWN import.~~ Implemented.
2. ~~Mastering analysis for one performance's stems.~~ Implemented.
3. ~~Phase-aware pseudo-3D spectrum.~~ Implemented.
4. ~~Image-to-spectrogram.~~ Implemented.
5. ~~Oscilloscope music.~~ Implemented.
6. ~~Mastering processing.~~ Implemented.

Additional constraints:

- Wavetable YAWN import is a first-class requirement for phase 9; YAWN may be
  modified to consume VWaveEditor wavetables.
- Wavetable morphing must support both ordered keyframes and free blending from
  the first release.
- Mastering starts with analysis on stems from one performance.
- Mastering is profile-based rather than one universal objective: loudness,
  definition/gaps, separation, fullness, and custom control.
- Rendering/automatic processing follows the analysis phase.

### Cross-cutting implemented

- Image, oscilloscope, and synth-note tracks can store versioned recipes.
- Recipes persist in timeline projects and regenerate through
  `regenerate_track`/Process → Regenerate Active Track.

### Remaining decisions

- How real-time mastering automation should integrate with per-track DSP state
  and undo.
- How reference matching and standards-compliant LUFS metering should work.
- Whether generator recipes become shared first-class track metadata.
- Which standard third-party wavetable metadata profile to target for WAV export.

### Phase 15: Waveform editing and cut-up arrangement (implemented)

Stage 1 is implemented as destructive-but-undoable track editing:

- Selection edge handles, track trim handles, repeat handle, and whole-track
  stretch handle.
- Undoable move/copy/repeat range operations with deterministic replay.
- Zero-crossing snap and one-step zero-cross cut.
- Track-level stereo split into hard-panned mono tracks, preserving stereo
  balance.
- Mono-to-stereo combine with back/front/both padding and preserved offsets.
- Session, CLI, Lua/MCP bridge, and tests for the new operations.

Stage 2 is implemented as a document-owned clip arrangement:

- `Document::Clip` with source range, local timeline position, stretch ratio,
  gain, fades, and name.
- Clip rendering supports gaps and overlap mixing; stereo channel layout is
  preserved.
- Clip tools: make, select, move, split, delete, repeat, stretch, move to
  playhead, and flatten.
- Clip layout is stored in the active track's `Document` history, sharing its
  undo/redo stack and project persistence.
- Destructive sample edits require flattening while clips exist, keeping source
  ranges stable.
- Session/CLI/Lua/MCP commands and tests cover the arrangement lifecycle.

Remaining follow-up work is UI polish for clip gain/fade editing and more
automation presets; the cut-up arrangement model itself is implemented.

### Transport: click-to-seek and loop playback (implemented)

- Clicking the waveform, 2D spectrum, ruler, or overview positions the playhead
  while a drag still creates a selection or spectral region.
- Shift+drag selects a loop region in waveform, 2D spectrum, or ruler views; Loop
  Selection derives one from the current selection.
- Loop playback wraps in `AudioPlayer`, with `L` toggling and `Shift+L` setting
  the loop from the selection.
- The loop region is session state and persists until cleared or a project or
  audio file is loaded.

## Maintainability and async refactor (in progress)

- Extracted `ui/UiTheme`, `ui/UiFormat`, `ui/Widgets`, `ui/UiRenderer`,
  `ui/DialogController`, and `services/FileService` from the `App` monolith.
- Split menu/toolbar/footer/settings, context menu, sidebar, waveform, spectrum,
  wavetable, mastering, image, and scope drawing into `chrome/` and `views/`
  translation units. `App.cpp` is now around 3.5k lines.
- Added `vwave::JobSystem` with bounded workers, cancellation, progress, and
  exception propagation.
- Added `UiDispatcher` and moved image, scope, and wavetable-note rendering to
  the worker pool. Image rendering supports progress and cancellation.
- Extracted `LoopController` for loop state, clamping, and drag-region handling.
- Added `AnalysisService` and async overview, spectrogram, and phase-spectrogram
  jobs with stale-result rejection; waveform, spectrum, and 3D views use them.
- Added copy-on-write `Audio` storage, making analysis snapshots cheap and
  isolating worker reads from UI edits.
- Extracted `ClipController` for clip-tool state and clip editing actions.
- Extracted `AnalysisController` and moved beat/pitch analysis to background
  jobs with cached results.
- Extracted `StereoController` for padding mode and stereo/mono track conversion.
- Extracted `TrackController` for track selection, add/remove/empty-track actions,
  and per-track gain/pan control state.
- Extracted `PlaybackController` for seek/play/stop/update and loop-aware
  playback ranges; loop state remains in `LoopController`.
- Extracted `ClipArrangement`; `Document` now delegates clip validation,
  rendering, and edit transformations. `Document.cpp` is under 1.8k lines.
- Extracted Session command helpers and clip command handlers into
  `src/session/`; `Session.cpp` is down from 2,092 to 1,722 lines.
- Split track command handling into `SessionTrackCommands`; `Session.cpp` is
  down to 1,537 lines.
- Split analysis command handling into `SessionAnalysisCommands`; `Session.cpp`
  is down to 1,438 lines.
- Split selection/edit/processing commands into `SessionEditCommands`;
  `Session.cpp` is down to 1,185 lines.
- Split generator/analyzer command handling into `SessionGeneratorCommands`;
  `Session.cpp` is down to 448 lines.
- Extracted `SpectralController`, `MasteringController`, `ImageController`,
  `ScopeController`, `WavetableController`, and `SettingsController`.
- Split SDL event dispatch, keyboard input, and mouse press/up/motion/wheel
  handling into `apps/vwave/input/`.
- Current major files: `App.cpp` 1,352 lines, `Session.cpp` 327,
  `Document.cpp` 227, `Timeline.cpp` 157.
- Moved mastering analysis and processed-stem rendering to background jobs
  with stale-result rejection and timeline snapshots.
- Expanded editor smoke coverage with a generated stereo tone, all editor
  tabs, and the settings dialog.
- Next: remaining async/performance work.
