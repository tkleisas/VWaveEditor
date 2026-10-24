# Command-line reference

The CLI is a thin wrapper around the Session command layer. Times are in
seconds unless a frame count is explicitly named.

```text
vwave info <audio>
vwave process <audio> <out> [options]
vwave analyze <audio> [--beats] [--pitch]
vwave slices <audio> <directory> [--mode transients|beats|markers]
vwave wavetable <audio> <out.vwt> [--frame-size N] [--frame-count N]
vwave synth <wavetable> <out.wav> [--note MIDI] [--duration SEC]
vwave sweep <out.wav> [--seconds SEC] [--start-hz HZ] [--end-hz HZ]
vwave deconvolve <recorded> <sweep> <out.wav> [--length FRAMES]
vwave lua <script.lua> [audio]
vwave command <name> [json] [--input audio] [--output audio]
vwave --mcp
vwave --version
```

> The CLI operates on a single-document `Session`. Multi-track editing and
> project files are currently provided by the desktop `Timeline` editor. Track
> commands for the CLI are planned.


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

## wavetable

```sh
vwave wavetable pad.wav pad.vwt --frame-size 2048 --frame-count 16
vwave wavetable pad.wav pad.wav --frame-size 2048 --frame-count 16
```

A `.wvt` output is a self-contained wavetable. A `.wav` output writes
concatenated mono frames plus a metadata sidecar.

## synth

```sh
vwave synth pad.vwt note.wav --note 60 --duration 2
vwave synth pad.vwt chord.wav --note 67 --duration 4 --unison 3 --morph 0.5
```

MIDI note 60 is middle C when the wavetable uses the default root note.

## sweep and deconvolve

```sh
vwave sweep sweep.wav --seconds 5 --start-hz 20 --end-hz 20000
vwave deconvolve recorded.wav sweep.wav cab.wav --length 4096 --trim
```

`--length` is the output IR frame count. `--trim` aligns the IR peak at frame
zero.

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
