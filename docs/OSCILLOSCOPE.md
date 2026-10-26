# Oscilloscope music

VWaveEditor can render an XY path into stereo audio. The left and right channels
are the path's X and Y coordinates, so an XY oscilloscope draws the shape while
the audio plays.

## Paths

- **Circle**, **ellipse**, **Lissajous**, **spiral**, **figure eight**, and
  **line** generators
- **Polyline** mode: click in the XY canvas to add points; click near the first
  point to close the path
- Arc-length traversal so speed is uniform along the path
- Open or closed paths
- `.vscope` JSON paths (`format`, `version`, name, closed flag, points)
- Save/load from the Scope tab or load a dropped `.vscope` file

## Rendering

- Scan rate in Hz: how many path traversals per second
- Duration, gain, output sample rate
- Direction: forward, reverse, or ping-pong
- Raw direct waveform or band-limited rendering
- 1×–8× oversampling with a Butterworth low-pass before decimation when
  band-limited
- Optional one-pole smoothing
- Peak normalization
- Render to a new track at the playhead

The scan rate is the oscillator frequency; the path shape determines the
harmonic content and stereo image. For example, a circle at 120 Hz produces
roughly sine/cosine L/R channels.

## UI

The **Scope** tab (`6`) shows the path on an XY grid, exposes the shape and
render controls, supports hand-drawn polylines, and renders the result as a new
track.

## CLI

```sh
vwave scope circle.wav --shape circle --scan-hz 120 --duration 2
vwave scope lissajous.wav --shape lissajous --a 3 --b 2 \
    --scan-hz 90 --band-limit-hz 14000 --save-path shape.vscope
vwave scope custom.wav --path-file shape.vscope --raw --duration 4
```

Options: `--shape`, `--path-file`, `--point-count`, `--size`, `--a`, `--b`,
`--phase`, `--turns`, `--name`, `--duration`, `--scan-hz`, `--sample-rate`,
`--gain`, `--smoothing`, `--raw`, `--band-limit-hz`, `--oversample`,
`--direction`, `--no-normalize`, `--peak`, `--save-path`.

## Lua

```lua
vwave.apply("oscilloscope_render", {
    out = "/audio/scope.wav",
    shape = "lissajous",
    a = 3, b = 2,
    scan_hz = 90.0,
    duration = 2.0,
    band_limited = true,
    save_path = "/paths/lissajous.vscope",
    replace = true
})
```

## MCP

Use `vwave_oscilloscope_render`, or `vwave_command` with
`oscilloscope_render`. Custom paths can be supplied as an array of `[x, y]`
points.

## Limitations

- No text, SVG import, or multi-pass thickness tools yet.
- Polyline editing is click-only; no drag handles or undo for individual
  points.
- Path recipes are stored as `.vscope` files; generated tracks do not yet carry
  an embedded recipe.
- Editing existing stereo audio in the XY domain is future work.
- Render/analyze jobs are synchronous and do not yet expose progress/cancel.
