# VWaveEditor

A non-destructive wave editor, spectral workstation, wavetable lab, and
sound-design toolkit. It runs as a standalone application and links directly
into [YAWN](https://github.com/tkleisas/yawn) as an embeddable editing core.

VWaveEditor is in active pre-alpha development. The architecture and public
interfaces are being built test-first; see [docs/PLAN.md](docs/PLAN.md).

## Scope

- Non-destructive waveform editing with exact undo/redo
- Cut, copy, paste, trim, crop, insert, fades, gain, normalise, reverse,
  invert, silence, DC removal, resample
- Sample slicing from transients and beat grids
- Beat, onset, pitch, tempo, and slice analysis
- Time stretching (WSOLA and phase vocoder), pitch shifting, and PaulStretch
- STFT-based spectral editing, filtering, gating, and time-frequency selection
- Wavetable extraction, drawing, morphing, additive creation, and export
- Offline wavetable synthesis
- Exponential sweep generation and impulse-response deconvolution
- Linear transfer/model capture format with a future neural-capture backend
- Lua 5.4 scripting for app control and sample-level processing
- MCP stdio server so agents can inspect and edit audio
- Standalone SDL2 editor
- UI-free C++ core and C embedding shim for YAWN

## Planned surface

```
vwave_editor                     standalone editor
vwave open|info|process ...      command-line processor
vwave --lua script.lua file.wav  scripted editing
vwave --mcp                      MCP server over stdio
libvwave_core.a                  embeddable C++17 library
libvwave_c.a                     stable C ABI
```

## Architecture

```
Desktop UI / CLI / Lua / MCP
             |
       Session command API
             |
          vwave_core
  audio model, history, DSP, analysis,
  spectral, wavetable, synthesis, capture
```

The core has no SDL or window-system dependency. All frontends call the same
operations, so behavior is consistent between the UI, scripts, and agents.

The document model keeps the source buffers immutable and replays a validated
operation history. Undo/redo is exact, and project files can preserve the full
edit stack.

## Build

VWaveEditor builds with CMake 3.20+ and C++17. System dependencies:

- libsndfile
- SDL2 + SDL2_ttf (standalone editor only)
- Lua 5.4 sources are vendored for reproducible script builds

On Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libsndfile1-dev \
    libsdl2-dev libsdl2-ttf-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Core-only build:

```sh
cmake -S . -B build -DVWAVE_BUILD_GUI=OFF
```

## Development method

Every feature starts with an executable contract test. Numeric DSP tests use
synthetic signals with known answers, such as sine waves, impulses, and click
trains. No binary fixtures are required. The current milestone and acceptance
criteria are tracked in [docs/PLAN.md](docs/PLAN.md).

## Embedding

```cpp
#include <vwave/Document.h>
#include <vwave/AudioIO.h>

vwave::Audio source;
if (!vwave::readAudio("take.wav", source)) return false;

vwave::Document doc = vwave::Document::fromAudio(std::move(source));
doc.setSelection(48000, 96000);
doc.normalize(-1.0);
doc.reverseSelection();
doc.apply(vwave::Operation::timeStretch(2.0, doc.selectedRange(),
                                        vwave::StretchAlgorithm::PhaseVocoder));

const vwave::Audio& result = doc.audio();
```

The edit history remains available for interactive undo after embedding.

## Status

This repository currently contains the project plan, build scaffold, public
contracts, and test harness. Implementation is landing phase-by-phase against
the tests in `tests/`.

## License

MIT. See [LICENSE](LICENSE). Vendored dependencies retain their own licenses;
see [NOTICES.md](NOTICES.md).
