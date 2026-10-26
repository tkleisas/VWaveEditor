# Future ideas and design notes

This document records features discussed for future implementation. Nothing in
this file is implemented unless stated otherwise. The guiding principle is
maximum user control: quality aids such as phase alignment, normalization, and
band-limiting must be available but never forced, because aliasing, cancellation,
and spectral artefacts can be intentional sound-design tools.

## Agreed direction (2026-10-25)

- The **wavetable designer, YAWN import, mastering analysis and processing,
  phase-aware 3D spectrum, image-to-spectrogram synthesis, and oscilloscope
  music are implemented**. Remaining work is advanced enhancement and deferred
  platform integration, not an outstanding planned phase.
- Wavetables import into YAWN through the versioned `.vwt` format and YAWN's
  Wavetable Synth Custom slot.
- Wavetable morphing supports **both ordered keyframe morphing and free
  multi-wave blending from day one**.
- Quality and artefact controls remain optional. Raw aliasing, phase
  cancellation, quantization, and unnormalized morphs are supported choices.
- The **mastering workflow begins with analysis**, not automatic processing.
  The initial target is a set of stems from one performance.
- Mastering has no single universal objective. The analyzer offers profiles
  for loudness/fullness, definition/gaps, separation, and custom control, with
  explicit constraints and a user-selected profile. Reference matching remains
  future work.

## 1. Wavetable designer and morph lab

Implemented. The engine stores constant-size frames with explicit keyframe
positions, ordered morphing, and free multi-frame blending. The synth exposes a
morph position and optional free weights. The designer UI exposes mode,
interpolation, keyframe, quality, and free-blend controls.

### Sources and keyframes

- Import a single cycle from the active audio selection.
- Draw a cycle directly.
- Generate from harmonics with per-harmonic amplitude and phase.
- Import multiple keyframes at once.
- Create blank/init cycles.
- Name, reorder, duplicate, delete, and mute keyframes.
- Store a source reference/recipe for reproducibility.
- Support 2 or more waves; no hard maximum beyond memory/practical UI limits.

### Morph models

- Ordered keyframe morph: a position parameter moves through A → B → C → D with
  interpolation between adjacent keyframes.
- Free blend: independent levels for any number of keyframes, normalized or
  unnormalized.
- 2D morph grid/pad: keyframes placed on an XY surface, with bilinear morphing.
- Morph trajectories: editable position/time curves, LFOs, envelopes, MIDI
  velocity/CC, and automation.
- Per-keyframe gain, phase rotation, DC offset, and spectral tilt.
- Per-keyframe loop/ping-pong behavior for morph curves.

### Interpolation modes

Provide all of these as explicit choices:

- Linear time-domain crossfade.
- Equal-power crossfade.
- Phase-aligned crossfade:
  - fundamental alignment
  - strongest-partial alignment
  - cross-correlation search
  - FFT phase rotation
- Spectral morph:
  - interpolate magnitudes
  - interpolate phases independently or phase-locked
  - phase unwrapping or shortest-arc interpolation
  - transient/phase-coherence controls
- Harmonic/coefficient morph:
  - interpolate harmonic amplitude and phase
  - optional sparse/dense harmonic handling
- Additive/difference morph:
  - A + (B - A) × t
  - signed differences
- Free mathematical mix expression per sample/frame, eventually scriptable.

### Quality and “intentional artefact” controls

Do not force correction. Expose:

- Phase alignment: off, auto, manual per keyframe.
- Normalization: off, per keyframe, per morph result, peak, RMS, LUFS.
- DC removal: off/on per cycle and after morphing.
- Aliasing mode:
  - raw table read (alias intentionally)
  - band-limited per octave/mipmap
  - oversampled synthesis then decimate
  - optional post-filter/slew
- Sample-rate conversion quality for frames:
  - nearest/linear
  - sinc
  - raw resample
- Bit depth/reduction and quantization options.
- Optional phase randomization.
- Optional per-cycle transient/zero-crossing rotation.
- Morph interpolation can cross through cancellation, silence, or unstable
  cycles if the user chooses it.
- Preview warnings are informational only; never block rendering.

### Designer UI

- Keyframe strip with thumbnails and morph trail.
- Large cycle editor with drawing.
- Harmonic spectrum editor.
- Morph position control with automation/LFO.
- A/B compare.
- “Import selection”, “Align”, “Normalize”, “Remove DC”, “Render”.
- 2D/3D view of how the waveform evolves across the morph.
- Preview note controls (MIDI note, root, duration, unison, level, morph).
- Save/load designer presets.

### Synthesis and persistence

- Extend `.vwt` to a versioned v2 format with keyframes, recipes, morph mode,
  interpolation mode, and quality/artefact settings.
- Keep loading v1 tables.
- Optionally pre-render a dense table for real-time oscillators or render
  band-limited frames on demand.
- Export:
  - `.vwt` designer project
  - WAV frame sequence
  - rendered note/audio
- Lua/MCP commands for keyframes, morphing, and rendering.

### Tests

- Endpoint morphs equal the source keyframes.
- Adjacent morphs are bounded and finite under every mode.
- Phase-alignment modes maximize selected correlation.
- Raw aliasing mode is deterministic and intentionally not band-limited.
- Equal-power/linear/normalized levels match the chosen contract.
- v1/v2 persistence round-trips.
- Morph automation renders deterministically.

## 2. Image → spectrogram

**Implemented first phase:** image decoding, pixel-buffer core synthesis,
luminance/alpha mapping, crop/flip, linear/log frequency and direct/mirrored/
looped time mapping, Griffin–Lim/momentum/phase-locked-init/random/color phase,
image UI, CLI, Lua, MCP, and render-to-track. Sequences/collages, hue→pan,
separate band/track mapping, stored recipes, and in-app preview remain future
work.

Turn a picture into audio through inverse STFT synthesis.

### Inputs and mapping

- Decode PNG/JPEG/other images into pixels in the UI layer; keep the core
  UI-free by passing a pixel buffer.
- Luminance → magnitude.
- Optional color mapping:
  - hue → phase
  - hue → stereo pan
  - color channels → separate bands or tracks
- Alpha → gate/mask.
- Invert brightness, threshold, gamma, contrast, blur/sharpen.
- Frequency mapping: linear, logarithmic, custom breakpoints.
- Time mapping: direct, mirrored, looped, stretched to a requested duration.
- FFT size/hop, window, overlap, sample rate, duration.
- Image orientation and crop.
- Multiple images in a sequence or collage.

### Phase reconstruction

- Griffin–Lim iterative phase recovery.
- Fast Griffin–Lim.
- Phase-locked/PGHI-style reconstruction.
- Random/texture phase.
- Color-derived phase.
- Optional stereo phase correlation controls.
- Parameterized phase smoothing and transient handling.

### Output and UX

- Preview before committing.
- Generate as a new track at the playhead/selection position.
- Store the image recipe/settings for later regeneration.
- Show the generated spectrogram next to the source image.
- Lua/MCP/CLI generator command.

### Risks

- Recognizability depends on time-frequency resolution.
- Sharp image edges create broadband clicks/noise.
- Phase reconstruction can sound metallic or smeared.
- Images may need target-frequency remapping, not a naive pixel-to-bin stretch.
- Large images need limits/downsampling and progress reporting.

## 3. Oscilloscope music lab

**Implemented first phase:** generated and hand-drawn XY paths render to stereo
with arc-length traversal, scan-rate control, direction modes, raw/band-limited
output, `.vscope` persistence, UI/CLI/Lua/MCP automation, and render-to-track.
Text/SVG import, multi-pass thickness, embedded recipes, and XY editing of
existing stereo audio remain future work.

Visualize and edit stereo audio as an XY vector path.

- 2D vectorscope with zoom and time selection.
- Render geometric paths to stereo audio.
- Parametric curves, polylines, Lissajous figures, text, and potentially SVG
  import.
- Arc-length traversal, speed, direction, loop points, passes, trails.
- Path smoothing and band-limited synthesis.
- Optional raw/aliased mode for intentional harshness.
- Multiple line thicknesses via repeated passes, offsets, or modulation.
- Edit existing stereo audio in the XY domain.
- Separate visual-fidelity and musical-quality targets.
- Render to a new track with a stored path recipe.
- Tests for path coverage, loop closure, continuity, and finite output.

## 4. Pseudo-3D magnitude/phase spectrum

**Implemented first phase:** the Spectrum tab has a bounded phase STFT with
principal phase, instantaneous frequency, and confidence; a CPU waterfall shows
magnitude height, phase hue, and confidence alpha; hovering reads the nearest
bin. Masking overlays and in-surface selection remain future work.

Extend the spectrogram beyond magnitude-only rendering.

### Data

- Magnitude and dB.
- Phase.
- Instantaneous frequency.
- Phase coherence/confidence.
- Magnitude masking/noise floor.
- Per-track and mix views.

### Views

- 3D waterfall of spectra.
- Surface/height map of magnitude over time and frequency.
- Phase encoded as color, with magnitude as height/brightness.
- Separate phase wheel/disk for a selected point.
- Magnitude and phase shown as coordinated surfaces.
- Time-frequency region selection linked to spectral editing.
- Orbit, zoom, perspective, hidden-line/transparency options.
- CPU software rendering first; move to OpenGL if density requires it.

### Constraints

- Phase is cyclic and unstable at low magnitude; display confidence/masking.
- Dense meshes need decimation and caching.
- Color alone is not enough to communicate phase; provide numeric/phase-wheel
  readouts.

## 5. Mastering analysis and spectral allocation

**Analysis and offline processing implemented:** the analyzer reports
logarithmic band power, masking/dominance, cross-stem phase coherence, per-stem
RMS/peak/true peak/crest/correlation, and profile-specific gain suggestions.
Processing applies static per-band curves as derived stems with an optional
common mix ceiling. Reports/manifests are available from the Mastering tab,
CLI, Lua, MCP, and Session.

Analyze a performance spread across multiple tracks (for example four stem
WAVs) and propose or apply dynamic spectral balance.

> Important distinction: equalization cannot create net energy from nothing.
> “Maximum total power” must be defined as an objective under constraints, such
> as loudness after true-peak normalization, spectral separation, or reduced
> masking. This is a core design decision.

### Analysis inputs

- N tracks/stems at the same project sample rate.
- Optional role/priority metadata (kick, bass, vocal, lead, pad, etc.).
- Optional reference track or target curve.
- Per-band loudness, power, masking, phase correlation, and spectral overlap.
- Time-varying analysis with attack/release/lookahead.
- Stereo/mono compatibility.
- True-peak and inter-sample peak awareness.
- Optional perceptual weighting and ITU-R BS.1770 loudness.

### Modes

- **Maximize loudness under a true-peak ceiling**:
  redistribute/compress spectral peaks and apply makeup gain.
- **Maximize spectral separation**:
  dynamically attenuate overlapping bands while preserving each track’s
  dominant/prominent bands.
- **Create frequency gaps for definition**:
  assign bands to a dominant track and duck the others, with smooth transitions.
- **Complementary EQ**:
  tracks share the spectrum; each band is allocated primarily to one source.
- **Masking-aware dynamic EQ**:
  use a psychoacoustic masking model to find where definition is lost.
- **Priority-based ducking**:
  user marks important tracks; others get dynamic notches.
- **Reference matching**:
  match overall spectral balance/loudness to a reference, without copying its
  timbre blindly.
- **Manual analysis only**:
  display conflicts and suggested curves without applying anything.

### Objective functions and constraints

Expose these explicitly:

- Target: summed power, perceptual loudness, spectral flatness, spectral
  contrast, masking reduction, separation, mono compatibility.
- Per-track maximum boost/cut.
- Per-band maximum boost/cut.
- Total true-peak ceiling.
- Minimum phase / linear phase.
- Smoothness and attack/release rates.
- Preserve transient energy.
- Preserve stereo image and phase relationships.
- Never create clipping.
- Optional “raw” mode that allows aggressive/non-physical gain curves.

### Processing architecture

- **Analysis/preview first**:
  compute masking maps, per-track overlaps, and suggested dynamic EQ; no audio
  modification.
- **Render to new tracks**:
  apply the derived per-track spectral gains offline and add processed tracks,
  preserving the originals.
- **Apply to mixdown**:
  master-bus or multi-source spectral balancing where source separation is not
  needed.
- **Track automation later**:
  a real-time per-track dynamic EQ/automation layer requires new per-track DSP
  state and undo integration.

### Dynamic EQ design choices

- STFT gain masks (frequency-domain) versus time-domain filter banks.
- Linear-phase FIR dynamic EQ for transient consistency.
- Minimum-phase filters for lower latency.
- Lookahead, smoothing, and gain interpolation to avoid pumping.
- Frequency-dependent attack/release.
- Cross-track sidechain detection.
- Zero-latency monitoring versus offline-only rendering.

### UI

- Multi-track spectrogram overlay.
- Masking/overlap heatmap.
- Per-band ownership/priority map.
- Suggested per-track EQ curves.
- Before/after loudness, true peak, phase correlation, mono compatibility.
- Preset goals: “maximize loudness”, “more definition”, “open gaps”,
  “preserve timbre”, “reference match”.
- Preview bypass.
- Commit as new tracks or automation.

### Tests

- Synthetic multi-tone stems with known overlap.
- Loudness/true-peak constraints are respected.
- Priority-based ducking reduces overlap by the requested amount.
- No NaNs/instability at silence.
- Smooth gain transitions avoid clicks.
- Stereo/mono sums remain finite.
- Deterministic renders from saved analysis recipes.
- Undo/redo and project persistence once automation is integrated.

### Decisions and remaining questions

Resolved:

- The input is stems of one performance.
- The first version is analysis/recommendation only.
- There is no single objective; multiple goal profiles are required.
- Source tracks are preserved; processing will come later.

Remaining:

- Whether later processing renders derived tracks or adds real-time track
  automation.
- How much spectral analysis should be aware of source roles/instrument labels.
- Reference matching.
- Standards-compliant LUFS/true-peak metering.

## 6. Cross-cutting infrastructure

- ~~Generator recipes stored with tracks so image/oscilloscope/wavetable
  results can be regenerated.~~ Implemented for image, oscilloscope, and synth
  tracks; mastering-stem recipes remain future work.
- Common parameter-preset system and JSON serialization.
- Lua, MCP, and CLI access for every generator/analyzer.
- Progress reporting and cancellation for long offline jobs.
- Cached spectrograms and incremental analysis.
- Peak/true-peak/loudness metering shared by mastering and export.
- Undo-friendly application paths, preferably derived tracks first.
- Versioned file formats with backwards compatibility.

## Implementation sequence

1. ~~Wavetable designer, YAWN import, morph modes, artefacts, UI, persistence,
   and automation.~~ Implemented.
2. ~~Mastering analysis: analysis-only masking/spectral-balance assistant for one
   performance's stems.~~ Implemented.
3. ~~Phase-aware pseudo-3D spectrum: phase/confidence data and a
   software-rendered 3D view, feeding later mastering diagnostics.~~
   Implemented.
4. ~~Image → spectrogram: image import, mapping controls, phase
   reconstruction, preview, and generated track recipes.~~ Implemented.
5. ~~Oscilloscope music: path/vector generator, XY editor, band-limited/raw
   synthesis, and preview.~~ Implemented.
6. ~~Mastering processing: derived-track renders, then track
   automation/dynamic EQ when the automation infrastructure exists.~~
   Offline derived-track rendering implemented; real-time automation remains
   future work.

## Decisions needed next

- How reference matching and standards-compliant LUFS metering should work.
- Image sequences/collages and hue-to-pan mapping.
- How to align stems if their offsets are not sample-aligned.
- Whether later mastering processing produces derived tracks or automation.
- Which third-party wavetable metadata profile to target for WAV export.

## 7. YAWN wavetable interoperability

The designer output must be usable in YAWN, not only in VWaveEditor.

### Formats

- Native `.vwt` v2:
  - keyframes, frame size, sample rate, root note, morph mode, interpolation
    mode, quality/artefact settings
  - optional embedded source recipes
  - fully lossless for VWaveEditor
- WAV wavetable export:
  - concatenated mono frames
  - JSON metadata sidecar for VWaveEditor
  - optional standard metadata chunks (`clm `, `uhWT`, or a documented
    compatibility profile) for third-party wavetable importers
- A shared loader API in `vwave_core`:
  - `Wavetable::load`
  - frame extraction
  - validation and versioning

### YAWN changes (implemented)

- Added a Custom table slot after the five built-in tables.
- Added `WavetableSynth::loadCustomWavetable()` with `.vwt` v1/v2 parsing.
- Frames are resampled to YAWN's 256-sample engine size and capped at 16 frames.
- Added a **Load .vwt...** button to the Wavetable Synth display.
- Custom tables persist in YAWN projects and device presets through the
  instrument extra-state hook.
- Cross-project test loads a VWaveEditor v2 table, verifies frame data, selects
  the Custom slot, renders a note, round-trips the exported `.vwt`, and restores
  the preset extra state.

### Acceptance criteria

- A designed table loads VWaveEditor -> YAWN without silent frame loss within
  YAWN's 16-frame limit; larger tables are evenly reduced and resampled to 256
  samples per frame.
- Frame data, frame count, and the Custom table selection survive import.
- Root/morph metadata remain in `.vwt`; YAWN's oscillator currently flattens
  them into frames.
- Raw/non-normalized tables load without forced correction.
- A table exported as WAV+metadata is loadable through the VWave core
  `loadWav`/`loadAuto` path. YAWN's current UI imports `.vwt` directly; WAV+
  sidecar import can be added to the same dialog when needed.
