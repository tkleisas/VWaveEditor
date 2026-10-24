# VWaveEditor

[![CI](https://github.com/tkleisas/VWaveEditor/actions/workflows/ci.yml/badge.svg)](https://github.com/tkleisas/VWaveEditor/actions/workflows/ci.yml)

A non-destructive wave editor, spectral workstation, wavetable lab, synthesis
sketchpad, and impulse-response capture tool. It runs as a standalone desktop
editor and as an embeddable editing core for
[YAWN](https://github.com/tkleisas/yawn).

![Waveform editor](docs/images/waveform.png)

## What works today

- **Non-destructive editing**: immutable source, replayed edit history, exact
  undo/redo, clipboard, and project persistence.
- **Waveform operations**: cut, copy, paste, insert, delete, crop, gain,
  normalize, fade in/out, reverse, invert, silence, DC removal, resample.
- **Time and pitch**: WSOLA and phase-vocoder time stretching, pitch shifting,
  and PaulStretch.
- **Analysis**: spectral-flux onsets, tempo/beat-grid detection, YIN pitch
  detection, transient slicing, and slice export with a JSON manifest.
- **Spectral editing**: STFT gain, Butterworth low/high-pass masks, spectral
  gating, blur, and time-frequency region editing from the UI.
- **Wavetables**: cycle extraction with pitch detection, harmonic construction,
  per-frame drawing, morphing, `.vwt` projects, WAV export/import, and
  offline wavetable-note rendering.
- **Capture**: exponential sweep generation, FFT deconvolution, IR alignment,
  normalization, and capture manifests.
- **Automation**: Lua 5.4 scripting, a command-line interface, and an MCP
  stdio server using the same command layer.
- **Embedding**: UI-free C++17 core plus a stable C ABI for YAWN or other hosts.
- **Desktop editor**: SDL2/SDL2_ttf waveform, spectrogram, and wavetable views;
  playback; beat grid; markers; slice overlays; keyboard and drag editing.

![Spectral editor](docs/images/spectrum.png)

![Wavetable editor](docs/images/wavetable.png)

## Build

Requirements:

- CMake 3.20+ and a C++17 compiler
- libsndfile
- SDL2 + SDL2_ttf for the standalone editor
- Lua 5.4 is vendored in `third_party/lua54`

Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libsndfile1-dev \
    libsdl2-dev libsdl2-ttf-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Core-only build:

```sh
cmake -S . -B build -DVWAVE_BUILD_GUI=OFF -DVWAVE_BUILD_CLI=OFF -DVWAVE_BUILD_EXAMPLES=OFF
```

Build options:

| Option | Default | Purpose |
| --- | --- | --- |
| `VWAVE_BUILD_GUI` | `ON` | Standalone SDL2 editor |
| `VWAVE_BUILD_CLI` | `ON` | `vwave` command-line tool |
| `VWAVE_BUILD_TESTS` | `ON` | Test suite and CTest registration |
| `VWAVE_BUILD_LUA` | `ON` | Embedded Lua scripting |
| `VWAVE_BUILD_MCP` | `ON` | MCP server |
| `VWAVE_BUILD_EXAMPLES` | `ON` | Embedding examples |
| `VWAVE_WARNINGS_AS_ERRORS` | `OFF` | Strict compiler warnings |

Artifacts land in `build/bin`:

```sh
./build/bin/vwave_editor recording.wav
./build/bin/vwave --help
```

## Quick start

Open the editor:

```sh
vwave_editor
```

Drag an audio file onto the window, press `Ctrl+O`, or pass it on the command
line. Select audio by dragging in the waveform. Use the sidebar or shortcuts:

| Key | Action |
| --- | --- |
| `Space` | Play or stop |
| `1` / `2` / `3` | Waveform / spectrum / wavetable view |
| `Wheel` | Zoom waveform |
| `B` | Detect tempo and beat grid |
| `P` | Detect pitch |
| `T` | Slice at transients |
| `E` | Export slices (dialog) |
| `W` | Extract wavetable from selection/document |
| `M` | Add marker at playhead |
| `N` | Normalize |
| `R` / `I` | Reverse / invert |
| `F` / `Shift+F` | Fade in / fade out |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+S` / `Ctrl+Shift+S` | Export audio / save project |

## Command line

Process a file with any sequence of operations:

```sh
vwave process input.wav output.wav \
    --select 0.5 2.0 --normalize --fade-in --fade-out \
    --spectral-gate -45

vwave process input.wav stretched.wav \
    --select-all --stretch 2.0 --algorithm phase_vocoder

vwave analyze input.wav
vwave slices input.wav out/slices --mode transients
vwave wavetable input.wav table.vwt --frame-size 2048 --frame-count 16
vwave synth table.vwt note.wav --note 60 --duration 2
vwave sweep sweep.wav --seconds 5 --start-hz 20 --end-hz 20000
vwave deconvolve recorded.wav sweep.wav cabinet.wav --length 4096 --trim
```

The generic command surface is available directly:

```sh
vwave command gain '{"db":-6,"start":1.0,"end":2.0}' \
    --input in.wav --output out.wav
```

See [CLI reference](docs/CLI.md).

## Lua

```lua
vwave.load("/samples/break.wav")
vwave.select(0.0, 2.0)
vwave.normalize(-1.0)
vwave.apply("spectral_gate", { threshold_db = -45 })

local beats = vwave.analyze_beats()
vwave.apply("slices", { mode = "beats", bpm = beats.bpm })
vwave.export_slices("/samples/slices", "break")

vwave.map(0.0, 0.25, function(sample, frame, channel)
    return sample * 0.5
end)

vwave.save("/samples/break_edit.wav")
```

Run scripts from the CLI:

```sh
vwave lua edit.lua input.wav
```

See [Lua API](docs/LUA.md) and `examples/scripts/`.

## MCP

Run a local stdio MCP server:

```sh
vwave --mcp
```

Example client configuration:

```json
{
  "mcpServers": {
    "vwave": {
      "command": "/absolute/path/to/vwave",
      "args": ["--mcp"]
    }
  }
}
```

The server exposes document information, loading/saving, analysis, editing,
spectral tools, wavetable tools, sweep/IR capture, and a generic
`vwave_command` tool. The document is also exposed as a resource at
`vwave://document/info`.

See [MCP tools](docs/MCP.md) for the tool catalog and protocol examples.

## Embedding in YAWN

The UI-free library is the intended integration point. Add the project as a
subdirectory, link `vwave::core`, and keep a `vwave::Document` in the editor
state:

```cmake
set(VWAVE_BUILD_GUI OFF CACHE BOOL "" FORCE)
set(VWAVE_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(../VWaveEditor VWaveEditor)
target_link_libraries(yawn_core PRIVATE vwave::core)
```

```cpp
vwave::Audio audio(channels, frames, sampleRate);
for (int ch = 0; ch < channels; ++ch)
    std::copy_n(yawnBuffer.channelData(ch), frames, audio.channel(ch));

vwave::Document document = vwave::Document::fromAudio(std::move(audio));
document.setSelection(startFrame, endFrame);
document.normalize(-1.0);
document.reverseSelection();

const vwave::Audio& rendered = document.audio();
```

For rendering in YAWN's fw2 UI, use `Document`, `computePeaks`,
`computeSpectrogram`, and `BeatAnalysis` directly. The SDL2 standalone editor
is a reference frontend, not a widget that can be parented inside YAWN's SDL3
window.

All editing happens on the control/UI thread. Snapshot the rendered audio
before publishing it to an audio thread; `Document` is not thread-safe. A C ABI
is available in `include/vwave/vwave_c.h` for hosts that prefer an opaque
handle and JSON commands.

See [Embedding guide](docs/EMBEDDING.md) and `examples/yawn_embed.cpp`.

## Architecture

```
SDL2 editor / CLI / Lua / MCP
              |
        Session command API
              |
          vwave_core
  immutable source + edit history
  DSP | analysis | spectral | wavetable
  synthesis | capture | persistence
```

- The source buffer is never modified by an edit.
- Undo/redo replays a deterministic operation list.
- Projects store the source, operation history, selection, markers, and slices.
- Numeric DSP uses synthetic-signal tests (sines, impulses, click trains,
  chirps) rather than committed binary fixtures.
- The GUI does not own DSP state; the same operations are available to scripts
  and agents.

See [Architecture](docs/ARCHITECTURE.md) for the data model, algorithms, test
strategy, and known limitations.

## Status and roadmap

VWaveEditor is pre-1.0. The current build includes the complete editing core,
CLI, Lua, MCP, C ABI, and standalone editor described above. Deliberate
remaining work:

- neural amp/instrument capture training and inference (linear IR capture is
  implemented; a capture-manifest backend is in place for future models)
- real-time preview of spectral edits
- VST3/AU effect hosting inside the editor
- multichannel UI beyond stereo
- performance work for multi-hour files and spectral jobs
- collaborative editing

The test-first development plan is in [docs/PLAN.md](docs/PLAN.md).

## License

MIT. See [LICENSE](LICENSE). Vendored and linked dependencies retain their own
licenses; see [NOTICES.md](NOTICES.md).
