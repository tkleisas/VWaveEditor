# Lua API

The standalone CLI and MCP run scripts against a `Session`. The Lua standard
libraries are available. Scripts are trusted local code and can read/write
files through standard Lua libraries.

```lua
vwave.load("/samples/take.wav")
vwave.select(0.25, 1.75)
vwave.normalize(-1.0)
vwave.save("/samples/take_edit.wav")
```

Run:

```sh
vwave lua edit.lua take.wav
```

## Document and history

| Function | Description |
| --- | --- |
| `vwave.load(path)` | Load audio as a new document |
| `vwave.save(path, format?, bit_depth?)` | Export the rendered document |
| `vwave.info()` | Document info table |
| `vwave.select(start, end)` | Select seconds |
| `vwave.select_all()` | Select everything |
| `vwave.clear_selection()` | Clear selection |
| `vwave.undo()` / `vwave.redo()` | History |
| `vwave.apply(command, params)` | Invoke any Session command |
| `vwave.log(message)` | Write a diagnostic to stderr |

`info()` includes `sample_rate`, `channels`, `frames`, `duration`, `selection`,
`markers`, `slices`, `undo_count`, and `redo_count`.

## Edits

All processing functions accept an optional start/end range in seconds after
their required parameters. Without a range they use the active selection, or
the whole document for non-destructive sample processing.

```lua
vwave.gain(-6, 0.5, 1.5)
vwave.normalize(-1.0)
vwave.fade_in(0.0, 0.25)
vwave.fade_out(1.75, 2.0)
vwave.reverse()
vwave.invert(0.0, 0.1)
vwave.silence(0.2, 0.4)
vwave.dc_remove()
vwave.copy()
vwave.cut()
vwave.delete()
vwave.paste()
vwave.crop()
```

## Time and pitch

```lua
vwave.stretch(2.0, "wsola")
vwave.stretch(0.5, "phase_vocoder", 0.0, 1.0)
vwave.paulstretch(4.0)
vwave.pitch(12.0, "phase_vocoder")
vwave.resample(44100)
```

Algorithms: `wsola`, `phase_vocoder`, `paulstretch`.

## Spectral

```lua
vwave.spectral_gain(200, 2000, -6)
vwave.spectral_filter("lowpass", 4000)
vwave.spectral_gate(-45)
vwave.apply("spectral_blur", { time_radius = 1, frequency_radius = 3 })
```

Use `vwave.apply` for commands that do not have a dedicated helper, including
all CLI and MCP commands.

## Analysis and slicing

```lua
local analysis = vwave.analyze_beats(0.5)
print(analysis.bpm, analysis.confidence)

local pitch = vwave.detect_pitch()
print(pitch.frequency, pitch.confidence)

local result = vwave.slices("transients", 0.5)
print(#result.slices)
vwave.export_slices("/samples/slices", "take")
```

`detect_pitch()` uses the current selection if one exists. Ranges can be passed
as `vwave.detect_pitch(start, end)`.

Slice modes: `transients`, `beats`, `markers`.

## Wavetables and synthesis

```lua
vwave.wavetable("/samples/pad.vwt", 2048, 16)
vwave.apply("harmonics_wavetable", {
    path = "/samples/harmonic.vwt",
    frame_size = 2048,
    amplitudes = { 0, 1, 0.5, 0.33, 0.25 }
})
vwave.synth("/samples/pad.vwt", 60, 2.0, "/samples/note.wav")
```

Wavetable paths ending in `.wav` are exported as concatenated frames plus a
sidecar; other extensions use the self-contained `.vwt` format.

## Capture

```lua
vwave.sweep("/samples/sweep.wav", 5.0)
vwave.deconvolve("/samples/recorded.wav",
                 "/samples/sweep.wav",
                 "/samples/cabinet.wav")
```

Use `vwave.apply("deconvolve", {...})` for IR length, peak alignment, and a
capture manifest.

## Sample access and mapping

```lua
local value = vwave.sample(0, 1000)

vwave.map(0.0, 0.25, function(sample, frame, channel)
    return sample * 0.5
end)
```

`sample` takes a zero-based channel and frame. `map` calls the callback once per
channel per frame and commits the result as one undoable replacement operation.
If the callback raises an error or returns a non-number, the document is not
changed.

## Generic command examples

```lua
vwave.apply("insert_silence", { seconds = 0.5, at = 1.0 })
vwave.apply("add_marker", { time = 1.25, name = "drop" })
vwave.apply("generate_sweep", {
    out = "/samples/sweep.wav",
    seconds = 3.0,
    start_hz = 30,
    end_hz = 18000
})
```

The complete command catalog is exposed by the CLI, MCP, and
`Session::commandNames()`. The Source of truth is `src/Session.cpp`.
