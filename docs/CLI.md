# Command-line reference

The CLI is a thin wrapper around the Session command layer. Times are in
seconds unless a frame count is explicitly named.

```text
vwave info <audio>
vwave process <audio> <out> [options]
vwave analyze <audio> [--beats] [--pitch]
vwave mastering <project.vwdoc> [--profile NAME] [--out report.json]
vwave mastering-render <project.vwdoc> [--out-dir DIR] [--add-tracks]
vwave image <image> <out.wav> [options]
vwave scope <out.wav> [options]
vwave slices <audio> <directory> [--mode transients|beats|markers]
vwave wavetable <audio> <out.vwt> [--frame-size N] [--frame-count N]
vwave wavetable-design <spec.json> <out.vwt>
vwave synth <wavetable> <out.wav> [--note MIDI] [--duration SEC]
vwave sweep <out.wav> [--seconds SEC] [--start-hz HZ] [--end-hz HZ]
vwave deconvolve <recorded> <sweep> <out.wav> [--length FRAMES]
vwave project <subcommand> <project.vwdoc> ...
vwave lua <script.lua> [audio]
vwave command <name> [json] [--input audio] [--output audio]
vwave --mcp
vwave --version
```

The CLI session is timeline-backed. Single-document commands operate on the
active track. Track-specific commands live under `vwave project`.


## info

```sh
vwave info take.wav
```

Prints document information as JSON: sample rate, channels, frames, duration,
selection, history counts, markers, and slices.

## process

Operations apply in the order given.

```sh
vwave process input.wav output.wav \
    --select 0.5 2.0 \
    --gain -3 \
    --fade-in \
    --fade-out \
    --normalize -1

vwave process input.wav output.flac \
    --select 0.0 4.0 \
    --stretch 1.5 --algorithm phase_vocoder \
    --format flac --bit-depth int24

vwave process input.wav output.wav \
    --spectral-gain 200 2000 -6 \
    --spectral-gate -45

vwave process input.wav pitched.wav \
    --pitch 12 --algorithm phase_vocoder

vwave process input.wav paul.wav --paulstretch 4

vwave process input.wav loop.wav \
    --select 0.2 1.8 --crop --normalize
```

Options:

| Option | Description |
| --- | --- |
| `--select START END` | Set the selection |
| `--select-all` | Select the whole document |
| `--gain DB` | Apply gain to the selection or document |
| `--normalize [DB]` | Peak normalize (default -1 dBFS) |
| `--fade-in`, `--fade-out` | Linear fades over the selection |
| `--reverse`, `--invert` | Reverse or invert polarity |
| `--silence`, `--dc-remove` | Silence or remove DC offset |
| `--copy`, `--cut` | Clipboard operations |
| `--delete`, `--crop` | Delete or crop to the selection |
| `--stretch RATIO` | Time-stretch; use `--algorithm` |
| `--pitch SEMITONES` | Pitch-shift; use `--algorithm` |
| `--paulstretch RATIO` | PaulStretch time stretch |
| `--resample RATE` | Resample the document |
| `--spectral-gain LOW HIGH DB` | STFT band gain |
| `--spectral-filter TYPE CUTOFF` | `lowpass` or `highpass` |
| `--spectral-gate DB` | Per-frame spectral gate |
| `--spectral-blur TIME FREQ` | Spectral blur radii |
| `--format` | `wav`, `aiff`, `flac`, `ogg`, or `auto` |
| `--bit-depth` | `int16`, `int24`, or `float32` |

Algorithms: `wsola`, `phase_vocoder`, `paulstretch`.

## analyze

```sh
vwave analyze take.wav
vwave analyze take.wav --beats
```

Prints tempo, beat grid, onsets, and pitch as JSON. The current implementation
reports both sections by default; flags are accepted for compatibility.

## slices

```sh
vwave slices break.wav slices/ --mode transients
vwave slices break.wav slices/ --mode beats
```

Writes one WAV per slice and a `*_manifest.json` in the output directory.

## scope

```sh
vwave scope circle.wav --shape circle --scan-hz 120 --duration 2
vwave scope lissajous.wav --shape lissajous --a 3 --b 2 \
    --scan-hz 90 --band-limit-hz 14000 --save-path shape.vscope
vwave scope custom.wav --path-file shape.vscope --raw --duration 4
```

Renders an XY path to stereo audio. Options: `--shape`, `--path-file`,
`--point-count`, `--size`, `--a`, `--b`, `--phase`, `--turns`, `--name`,
`--duration`, `--scan-hz`, `--sample-rate`, `--gain`, `--smoothing`, `--raw`,
`--band-limit-hz`, `--oversample`, `--direction`, `--no-normalize`, `--peak`,
`--save-path`. See [Oscilloscope music](OSCILLOSCOPE.md).

## image

```sh
vwave image cover.png cover.wav --duration 8 --phase fast_griffin_lim
vwave image art.jpg art.wav --duration 12 --min-hz 40 --max-hz 16000 \
    --freq-scale logarithmic --time-map mirrored --channels 2
```

Renders an image into audio through inverse STFT. Brightness becomes magnitude;
hue can drive phase; alpha gates. Supported decoding includes PNG, JPEG, BMP,
TGA, GIF, PPM/PGM, and HDR. See [Image to spectrogram](IMAGE_SPECTROGRAM.md).

Options: `--duration`, `--fft-size`, `--hop`, `--sample-rate`, `--min-hz`,
`--max-hz`, `--freq-scale`, `--time-map`, `--loops`, `--gamma`, `--contrast`,
`--threshold`, `--invert`, `--no-alpha`, `--phase`, `--iterations`, `--seed`,
`--channels`, `--no-normalize`, `--peak`.

## mastering

```sh
vwave mastering session.vwdoc --profile definition --out mastering.json
vwave mastering session.vwdoc --profile separation --band-count 48     --max-cut 18 --smoothing 2 --include-muted
```

Analyzes aligned timeline stems without changing audio. Profiles are
`balanced`, `definition`, `separation`, `fullness`, `loudness`, and `custom`.
The report includes per-band masking, phase coherence, per-stem levels, and
suggested gain curves. See [Mastering analysis](MASTERING.md).

Options: `--profile`, `--out`, `--band-count`, `--fft-size`, `--hop`,
`--max-cut`, `--max-boost`, `--smoothing`, `--include-muted`.

### mastering-render

```sh
vwave mastering-render session.vwdoc --profile definition \
    --out-dir mastered --prefix song --ceiling -1
vwave mastering-render session.vwdoc --profile definition \
    --add-tracks --mute-originals --ceiling -1
```

Applies static profile gain curves and writes processed stem WAVs, a mastered
mix, and a JSON manifest. `--add-tracks` adds derived tracks at the original
offsets; `--mute-originals` mutes the source tracks. Additional options:
`--process-fft-size`, `--process-hop`, `--limit-mix`/`--no-limit`, `--ceiling`,
`--prefix`.

## wavetable

```sh
vwave wavetable pad.wav pad.vwt --frame-size 2048 --frame-count 16
vwave wavetable pad.wav pad.wav --frame-size 2048 --frame-count 16
vwave wavetable pad.wav pad.vwt --frame-size 2048 --frame-count 16 \
    --morph phase_aligned --interpolation cubic --root 57 \
    --oversample 4 --band-limit --max-harmonic 96 --quantize 12 \
    --align-phases
```

A `.vwt` output is a self-contained versioned wavetable. A `.wav` output writes
concatenated mono frames plus a metadata sidecar.

Wavetable options:

- `--morph linear|equal_power|phase_aligned|spectral|harmonic`
- `--interpolation nearest|linear|cubic`
- `--root MIDI` sets wavetable root metadata
- `--oversample N` stores the preferred render oversampling
- `--band-limit` applies a 64-harmonic limit; `--max-harmonic N` overrides it
- `--quantize BITS` quantizes frame samples
- `--align-phases` aligns every frame to the first frame
- `--no-normalize`, `--no-dc`, `--no-phase-align` disable extraction defaults

### wavetable-design

The design command accepts a JSON spec with `frames`, each containing either a
`harmonics` array (with optional `phases`) or a full `samples` array:

```json
{
  "name": "morph",
  "frame_size": 2048,
  "root_note": 60,
  "morph_mode": "equal_power",
  "interpolation": "cubic",
  "oversampling": 2,
  "frames": [
    {"harmonics": [0, 1.0], "position": 0.0},
    {"harmonics": [0, 1.0, 0.5, 0.25], "position": 0.5},
    {"harmonics": [0, 1.0, 1.0, 1.0], "position": 1.0}
  ]
}
```

```sh
vwave wavetable-design morph.json morph.vwt
```

## synth

```sh
vwave synth pad.vwt note.wav --note 60 --duration 2
vwave synth pad.vwt chord.wav --note 67 --duration 4 --unison 3 --morph 0.5
vwave synth pad.vwt blend.wav --note 60 --blend 1,0.5,0 --oversample 4
vwave synth pad.vwt table-root.wav --note 57 --use-table-root
```

MIDI note 60 is middle C when the wavetable uses the default root note.
`--use-table-root` tunes against the wavetable's stored root note instead.
`--blend` selects free multi-frame weights; the count must match the table.

## sweep and deconvolve

```sh
vwave sweep sweep.wav --seconds 5 --start-hz 20 --end-hz 20000
vwave deconvolve recorded.wav sweep.wav cab.wav --length 4096 --trim
```

`--length` is the output IR frame count. `--trim` aligns the IR peak at frame
zero.

## project regenerate

```sh
vwave project regenerate session.vwdoc
vwave project regenerate session.vwdoc 2
```

Re-runs the stored generator recipe for the active track or the given track
index and saves the project. See [Track recipes](RECIPES.md).

## project

Multi-track project automation works on `.vwdoc` timelines:

```sh
vwave project info take.vwdoc
vwave project tracks take.vwdoc
vwave project add-track take.vwdoc pad.wav --name Pad --start 0.5
vwave project add-empty-track take.vwdoc --name Recording
vwave project remove-track take.vwdoc 1
vwave project select-track take.vwdoc 0
vwave project set take.vwdoc 1 --gain -6 --pan -0.2 --mute
vwave project set take.vwdoc 1 --solo --arm --start 0.25
vwave project mixdown take.vwdoc mix.wav
```

Track indices are zero-based. `project set` accepts `--name`, `--gain`,
`--pan`, `--start`, `--mute`/`--unmute`, `--solo`/`--unsolo`, and
`--arm`/`--disarm`. Changes are saved back to the project.

## lua

```sh
vwave lua edit.lua take.wav
```

The audio file is loaded before the script starts. Without an input argument
the script can call `vwave.load(...)` itself.

## command

Direct access to any Session command:

```sh
vwave command select '{"start":0.5,"end":2.0}' --input take.wav
vwave command gain '{"db":-6}' --input take.wav --output quiet.wav
vwave command analyze_beats '{}' --input break.wav
```

The result JSON is printed. The command layer is the same one used by Lua, MCP,
and the desktop editor. See [Lua API](LUA.md) and [MCP](MCP.md).
