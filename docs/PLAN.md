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
- mobile frontends.

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

As of the v0.0.3 development line:

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
