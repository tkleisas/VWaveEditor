# MCP server

VWaveEditor exposes a local Model Context Protocol server over stdio. It uses
JSON-RPC 2.0 with newline-delimited messages and supports `initialize`,
`tools/list`, `tools/call`, `resources/list`, `resources/read`, `ping`, and
`prompts/list`.

```sh
vwave --mcp
```

Client configuration:

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

The server starts with an empty document. Use `vwave_load_audio` or the generic
command tool to open audio, or load a project.

MCP operates on a timeline-backed session. Document tools target the active
track; dedicated track tools add, select, configure, and remove tracks.


## Resources

| URI | Content |
| --- | --- |
| `vwave://document/info` | Current document, selection, history, markers, slices |

## Tools

| Tool | Purpose |
| --- | --- |
| `vwave_document_info` | Inspect document state |
| `vwave_load_audio` | Load an audio file |
| `vwave_save_audio` | Export the active track |
| `vwave_load_project` | Load a non-destructive project |
| `vwave_save_project` | Save source, history, and metadata |
| `vwave_select` | Set active-track selection in seconds |
| `vwave_tracks` | List tracks and routing |
| `vwave_add_track` | Add an audio file as a new track |
| `vwave_add_empty_track` | Add an empty track |
| `vwave_select_track` | Set the active track |
| `vwave_remove_track` | Remove a track |
| `vwave_track_set` | Set name/offset/gain/pan/mute/solo/arm |
| `vwave_save_mixdown` | Render all audible tracks |
| `vwave_undo` / `vwave_redo` | History |
| `vwave_analyze_beats` | Tempo, onsets, beat grid |
| `vwave_detect_pitch` | Fundamental frequency |
| `vwave_slices` | Transient, beat, or marker slicing |
| `vwave_export_slices` | Write slice WAVs and manifest |
| `vwave_wavetable` | Extract or construct a wavetable |
| `vwave_wavetable_design` | Build a multi-keyframe wavetable |
| `vwave_synth_note` | Render a wavetable note |
| `vwave_mastering_analysis` | Analyze stem masking, phase, and suggested gains |
| `vwave_mastering_render` | Render processed mastering stems/files |
| `vwave_image_spectrogram` | Render an image into audio through inverse STFT |
| `vwave_oscilloscope_render` | Render an XY scope path to stereo audio |
| `vwave_generate_sweep` | Create an exponential sweep |
| `vwave_deconvolve` | Extract a linear impulse response |
| `vwave_command` | Execute any Session command |
| `vwave_run_lua` | Run trusted Lua against the document |

`vwave_command` is the escape hatch that exposes every command. It takes:

```json
{
  "command": "spectral_gain",
  "parameters": {
    "low_hz": 200,
    "high_hz": 2000,
    "db": -6
  }
}
```

## Example exchange

Initialize:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}
```

The response reports `serverInfo.name = "vwave"` and tools/resources
capabilities. The client then sends:

```json
{"jsonrpc":"2.0","method":"notifications/initialized"}
```

Load and inspect:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"vwave_load_audio","arguments":{"path":"/samples/take.wav"}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"vwave_document_info","arguments":{}}}
```

Analyze and slice:

```json
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"vwave_analyze_beats","arguments":{"sensitivity":0.5}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"vwave_slices","arguments":{"mode":"beats","bpm":120,"beats_per_slice":1}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"vwave_export_slices","arguments":{"directory":"/samples/slices","prefix":"take"}}}
```

Edit through the generic tool:

```json
{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"vwave_command","arguments":{"command":"gain","parameters":{"db":-3,"start":0.5,"end":1.5}}}}
```

Track-aware example:

```json
{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"vwave_add_track","arguments":{"path":"/samples/pad.wav","name":"Pad","start":0.5}}}
{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"vwave_track_set","arguments":{"index":1,"gain_db":-6,"pan":-0.2,"muted":true}}}
{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"vwave_save_mixdown","arguments":{"path":"/samples/mix.wav"}}}
```

Each successful `tools/call` returns text content containing the JSON result and
`isError: false`. Command failures return `isError: true` with the error message
as text. Unknown tools and malformed JSON-RPC requests return structured errors.

## Command model

All tools except `vwave_run_lua` call the same `vwave::Session` command layer as
the CLI and Lua. Commands are non-destructive and undoable. User-facing times
are seconds. File paths are resolved by the process, so the MCP client should
have permission to access the requested files.

## Security

The server executes local file I/O and trusted Lua. Run it under the user's
normal filesystem permissions. Do not expose the stdio process to untrusted
clients that should not be able to read or write local files.

## Transport details

Messages are newline-delimited. The implementation also accepts
`Content-Length` framed bodies for clients that use that convention.
