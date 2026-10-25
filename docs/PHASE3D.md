# Phase-aware 3D spectrum

The Spectrum tab has a **View: 2D / 3D** toggle. The 3D view is a CPU-projected
waterfall of the active track's phase-aware STFT. It is a visualization; the 2D
time-frequency selection and spectral editing controls remain the editing path.

## Data

`vwave::computePhaseSpectrogram()` computes:

- `decibels` — Hann-windowed STFT level per frame/bin
- `phases` — principal phase per bin
- `instantaneousHz` — phase-difference frequency estimate per frame/bin
- `confidence` — magnitude/phase-stability confidence, 0–1

The STFT uses a phase-safe fixed hop (half the FFT size). Display frames are
strided to cap the cache at `maxFrames` (default 1536) on long files, while
instantaneous frequency is calculated from adjacent analysis frames before
striding. Power/phase use a mono downmix of the active track.

## View

- X axis: logarithmic frequency, using the current Spectrum frequency range
- Depth: time
- Height: band level
- Color: phase (see the `-pi` … `+pi` legend)
- Alpha: phase confidence
- Hovering the surface shows frequency, level, confidence, phase, and the
  instantaneous-frequency estimate.

The view is analysis-only. It does not render audio or create an edit.

## API

```cpp
vwave::PhaseSpectrogram phase =
    vwave::computePhaseSpectrogram(audio, 2048, 1536);
float db = phase.dbAt(frame, bin);
float radians = phase.phaseAt(frame, bin);
float hz = phase.instantaneousHzAt(frame, bin);
float confidence = phase.confidenceAt(frame, bin);
```

## Limitations

- CPU projection; no OpenGL surface renderer yet.
- Phase confidence is heuristic, not a statistical phase-coherence estimate.
- The view is an overview: frames/bin resolution is capped for bounded memory.
- No time-frequency selection inside the 3D surface yet.
