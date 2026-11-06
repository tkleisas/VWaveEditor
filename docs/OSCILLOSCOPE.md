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

## Composition timeline

The core now has a versioned composition model for animated scope drawings:

- `ScopeDrawing`: one or more strokes; multi-stroke travel is rendered as a
  direct connector line, so pen jumps are audible/visible as intentional
  glitch or percussive artifacts.
- `ScopeKeyframe`: time, drawing id, transform, gain, easing, and transition.
- `ScopeTransform`: translation, X/Y scale, rotation, and pivot.
- Easing: hold, linear, ease-in, ease-out, ease-in-out, smoothstep.
- Path transitions: **hold/cut** for deliberate clicks, or **crossfade** for a
  blended morph between drawings.
- `renderScopeComposition` renders the animation with the existing scan,
  direction, smoothing, oversampling, band-limit, and normalization pipeline.
- `.vscope` version 2 stores compositions; version 1 single paths load as a
  one-drawing, one-keyframe composition.
- Composition recipes (`oscilloscope_composition`) render the animated result
  through the normal track-recipe pipeline.

Keyframe transition/easing belongs to the segment **starting at that keyframe**.
The next UI stage adds the keyframe strip, transform gizmo, image/SVG import,
and XY audio verification view.

## Raster image tracing

`ScopeTrace` converts a raster image into a monochrome contour drawing:

- alpha compositing over white, luminance conversion;
- threshold, invert, gamma, and contrast controls;
- optional working-resolution cap;
- pixel-edge contour tracing for filled shapes, including separate contours for
  multiple components and holes;
- collinear simplification, optional distance simplification, point caps, and
  nearest-neighbour stroke ordering;
- output is a `ScopeDrawing` with multiple closed strokes.

Multi-stroke travel is rendered as a direct connector, which is intentional:
on a stereo XY display there is no separate beam-blanking channel.

## SVG import

`ScopeSvg` imports SVG geometry directly into a `ScopeDrawing`:

- supported elements: `path`, `line`, `polyline`, `polygon`, `rect`, `circle`,
  `ellipse`;
- nested group transforms: translate, scale, rotate, matrix, skewX, skewY;
- SVG path commands: M/L/H/V, C/S, Q/T, A, and Z, including relative forms;
- cubic/quadratic curves and arcs are flattened at a configurable resolution;
- multiple subpaths become multiple strokes;
- coordinates are normalized to the scope's -1..1 drawing space.

Ignored: text, gradients, filters, masks, CSS styling, and external resources.
Fills and strokes are imported as geometry outlines for now.

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
track. **View: Design / XY Audio** switches between the designed path and an
accumulated XY image of the active stereo track's recorded output, which is the
verification view for rendered scope tracks.

**Import Image** traces a raster image into the current scope path. **Import
SVG** loads SVG geometry. Imported multi-stroke drawings are flattened into the
current editable path, with travel connectors retained as intentional glitch
lines. Imported paths do not show per-point editing dots.

## XY verification

`ScopeXY` converts stereo audio into an accumulation plot:

- left channel = X, right channel = Y;
- configurable range, gain, stride, and line connection;
- intensity accumulation at viewport resolution;
- the Scope tab caches the plot as a texture keyed by track, document revision,
  and viewport size.

This is how animation, transform interpolation, contour travel, and clicks can
be inspected as they will appear on an XY oscilloscope.

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

- Raster tracing is contour-only for now; no skeleton/centerline mode yet.
- SVG import supports a geometry subset; text, filters, gradients, and CSS
  styling are ignored.
- No multi-pass thickness tools yet.
- Polyline editing is click-only; no drag handles or undo for individual
  points.
- Path recipes are stored as `.vscope` files; generated tracks do not yet carry
  an embedded recipe.
- Editing existing stereo audio in the XY domain is future work.
- Render/analyze jobs are synchronous and do not yet expose progress/cancel.
- Image/SVG tracing is a core operation but not yet wired into the Scope tab UI.
- Keyframe timeline strip and transform gizmo are not yet wired; composition
  authoring currently requires core/recipe data.
- Image/SVG import currently replaces the single editable scope path rather
  than adding a drawing asset to the composition timeline.
