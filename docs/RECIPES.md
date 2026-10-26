# Track recipes

Generated tracks can store a versioned JSON recipe describing how to recreate
their audio. Recipes survive timeline project save/load.

## Supported recipes

- `image_spectrogram` — image path plus all ImageSpectrogramOptions
- `oscilloscope_render` — embedded `.vscope`-equivalent points plus render options
- `synth_note` — wavetable path, MIDI note, duration, and synth options

Wavetables themselves are persisted by the wavetable tools; a synth note recipe
references the `.vwt` path used at render time.

## UI

Tracks created from the Image or Scope tabs store a recipe automatically.
**Process → Regenerate Active Track** recreates the active track's audio from its
recipe, preserving its timeline offset and recipe.

The recipe is not part of `Document` undo history. Regeneration replaces the
track audio directly.

## CLI

```sh
vwave project regenerate session.vwdoc
vwave project regenerate session.vwdoc 2
```

## Lua/MCP/Session

```lua
vwave.apply("regenerate_track", { index = 0 })
```

The generic command surface exposes `regenerate_track`; MCP clients can call it
through `vwave_command`.

## Project format

Recipes are stored per track:

```json
{
  "name": "Scope",
  "startFrame": 0,
  "recipe": {
    "type": "oscilloscope_render",
    "version": 1,
    "scope_path": { "name": "Circle", "closed": true, "points": [[0.8, 0.0]] },
    "options": { "sample_rate": 48000, "duration": 2.0, "scan_hz": 120.0 }
  }
}
```

## Limitations

- Mastering processed-stem recipes are not regenerable yet.
- Hand-drawn scope recipes store points; generated shapes store their generated
  point set rather than the shape parameters.
- Recipe regeneration is deterministic only when the recipe's source files and
  random seed are still available.
- Recipe edits are not exposed in the UI yet; recreate from the generator to
  produce a new recipe.
