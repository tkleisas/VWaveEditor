# Image to spectrogram

VWaveEditor can render a picture into audio through inverse STFT synthesis.
Image decoding uses the vendored `stb_image`; synthesis is UI-free in
`vwave::imageToSpectrogram()`.

## Controls

- **Frequency**: linear or logarithmic mapping from image rows to bins
- **Time**: direct, mirrored (ping-pong), or looped
- **Phase**: Griffin–Lim, fast/momentum Griffin–Lim, phase-locked
  initialization, random, or hue-derived color phase
- **FFT**: 256–8192 samples; hop is one quarter of the FFT in the UI
- **Duration** (capped at 60 s), min/max frequency, loops
- **Gamma**, contrast, threshold, inversion, alpha gating
- **Horizontal/vertical flip**
- **Mono/stereo** output (stereo currently duplicates the mono signal)
- **Normalize** to a -0.9 dBFS-style peak target

The image is interpreted as:

- left → right: time
- bottom → top: low → high frequency
- brightness/luminance: magnitude
- transparency: additional gate
- hue: optional phase source

## Phase modes

- `griffin_lim` — iterative magnitude-constrained phase recovery
- `fast_griffin_lim` — momentum-accelerated Griffin–Lim
- `phase_locked` — phase propagation across frames before momentum Griffin–Lim;
  this is a practical phase-lock initialization, not a full PGHI implementation
- `random` — deterministic random phase
- `color` — hue-derived phase without iteration

## UI

The **Image** tab (`5`) loads and previews an image, exposes the controls above,
and renders the result as a new track at the playhead. Images can also be opened
or dropped directly.

## CLI

```sh
vwave image cover.png cover.wav \
    --duration 8 --min-hz 40 --max-hz 16000 \
    --freq-scale logarithmic --time-map direct \
    --phase fast_griffin_lim --iterations 32 --channels 2
```

Options: `--duration`, `--fft-size`, `--hop`, `--sample-rate`, `--min-hz`,
`--max-hz`, `--freq-scale`, `--time-map`, `--loops`, `--gamma`, `--contrast`,
`--threshold`, `--invert`, `--no-alpha`, `--phase`, `--iterations`, `--seed`,
`--channels`, `--no-normalize`, `--peak`.

## Lua

```lua
vwave.apply("image_spectrogram", {
    path = "/images/cover.png",
    out = "/audio/cover.wav",
    duration = 8.0,
    phase_mode = "fast_griffin_lim",
    channels = 2,
    replace = true
})
```

`replace = true` adds/replaces a timeline track in the Session.

## MCP

Use `vwave_image_spectrogram`; the generic `vwave_command` path accepts the same
parameters under `image_spectrogram`.

## Limitations

- Decoding happens in the core's image module; UI/CLI host code does not need
  to pass raw pixels.
- Stereo is duplicated mono; hue→pan and separate band/track mapping are future
  work.
- No image sequences/collages or stored regeneration recipe yet.
- No in-app generated-waveform preview; render to a track to inspect playback.
- Very large images and long Griffin–Lim jobs are synchronous and do not yet
  expose progress/cancel. Duration is capped at 60 s for bounded memory.
- Sharp image edges can create broadband clicks; phase reconstruction can sound
  smeared or metallic.
