# Mastering analysis and processing

VWaveEditor's mastering tool analyzes aligned stems from one performance and can
render the profile gain curves as processed derived tracks. The source tracks
remain untouched.

Analysis reports masking, phase coherence, levels, and profile-specific
per-band gain suggestions. Processing applies those static per-band curves
offline; real-time dynamic EQ/automation is future work.

## Input model

- Stems are timeline tracks. Track offsets define alignment.
- By default, muted tracks are excluded and solo state is respected. Enable
  `include_muted` to analyze every non-empty stem regardless of mute/solo.
- Spectral power/suggestions use the raw track audio, before track fader and
  pan. The mix metrics use the current audible mixdown, including faders/pan.
- All tracks share the project sample rate; adding a track at a different rate
  is resampled by `Timeline`.

## Bands and STFT

The analyzer uses a Hann-windowed STFT with logarithmic frequency bands
(default 2048 FFT, 2048 hop, 32 bands from 20 Hz to 20 kHz). Each band reports:

- `mix_power_db` — combined stem power in dBFS
- `masking_db` — combined power relative to the strongest stem (0 means no
  overlap)
- `phase_coherence` — weighted cross-stem phase alignment, 0–1

Each stem band reports `power_db`, `share_db`, `overlap_db`, whether it is the
band's dominant stem, and `suggested_gain_db`.

## Profiles

There is no single mastering objective, so the analyzer exposes profiles:

| Profile | Goal |
| --- | --- |
| `balanced` | Mild masking cleanup while preserving the existing balance |
| `definition` | Stronger separation of overlapping bands |
| `separation` | Aggressive gap creation between stems |
| `fullness` | Mild cuts plus limited boosts where the mix has spectral gaps |
| `loudness` | Reduce dense spectral peaks and masking before later limiting |
| `custom` | Use explicit strength/cut/boost settings |

The profile caps are used unless overridden. `max_cut_db` and `max_boost_db`
accept `0` to disable that direction; the UI passes 12 dB cut and 2 dB boost by
default. `smoothing_bands` averages suggestions across neighboring bands to
avoid one-band spikes.

## Levels and phase

- Per-stem RMS, sample peak, estimated true peak (4× cubic interpolation),
  crest factor, and stereo correlation.
- Mix RMS, sample peak, estimated true peak, and stereo correlation.
- Overall and per-band phase coherence.

True peak is an estimate from 4× cubic interpolation, not a standards-compliant
oversampled true-peak meter. Loudness is RMS dBFS, not LUFS.

## Processing

`renderMastering()` applies each stem's suggested band gains with a
Hann-windowed STFT gain mask and overlap-add reconstruction.

- Processed stems are derived copies; source tracks are preserved.
- The processed mix is checked against a ceiling using the same 4× cubic
  true-peak estimate as analysis.
- If the mix exceeds the ceiling, one common gain is applied to every processed
  stem and the mix, preserving their relative balance.
- Output can be written as individual stem WAVs, a mastered mix, and a JSON
  manifest.
- Session/UI rendering can add the processed stems back to the timeline at
  their original offsets, gain, pan, and solo state. Optionally mute the
  originals to audition the processed mix.
- Processing is static over the whole track; it is not a time-varying dynamic
  EQ or real-time automation yet.

## UI

The **Mastering** tab (`4`) provides:

- Profile and analysis settings in the inspector
- **Analyze Stems** in the Process menu or inspector
- Mix band levels with masking and phase overlays
- Per-stem suggested-gain cells: red for cuts, green for boosts, outlined cells
  for the dominant stem at that frequency
- Ceiling and mute-originals controls
- **Render Processed Stems** to derived timeline tracks
- **Export Report** as JSON

## CLI

```sh
vwave mastering session.vwdoc --profile definition --out mastering.json
vwave mastering session.vwdoc --profile separation \
    --band-count 48 --max-cut 18 --smoothing 2 --include-muted
vwave mastering-render session.vwdoc --profile definition \
    --out-dir mastered --prefix song --ceiling -1
vwave mastering-render session.vwdoc --profile definition \
    --add-tracks --mute-originals --ceiling -1
```

## Lua

```lua
local report = vwave.apply("mastering_analysis", {
    profile = "fullness",
    band_count = 32,
    out = "/reports/mastering.json"
})
vwave.log(report.summary)

local rendered = vwave.apply("mastering_render", {
    profile = "definition",
    out_dir = "/renders/mastered",
    prefix = "song",
    add_tracks = true,
    mute_originals = true,
    ceiling_db = -1.0
})
vwave.log(rendered.summary)
```

## MCP

Use `vwave_mastering_analysis` for analysis and `vwave_mastering_render` for
offline processing. Both are also available through `vwave_command`.

## Limitations

- Processing applies static profile curves, not time-varying dynamic EQ.
- Reference matching and standards-compliant LUFS metering are not implemented.
- True peak is an interpolation estimate; band power uses per-channel spectra,
  while phase coherence uses a mono downmix.
- Analysis and processing recipes are not persisted in the project. Re-run after
  changing stems, faders, mute/solo, or track offsets.
- Derived processing does not change source tracks. Undo history is per-track;
  adding/muting derived tracks is not part of track-document undo.
- Real-time mastering automation is future work.
