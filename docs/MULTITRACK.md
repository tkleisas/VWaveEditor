# Multi-track timeline

VWaveEditor's desktop editor is built on `vwave::Timeline`. Each track owns an
independent non-destructive `vwave::Document` plus playback/recording metadata.

## Track model

| Property | Meaning |
| --- | --- |
| `name` | Display name |
| `document` | Non-destructive source/history for the track audio |
| `startFrame` | Placement on the project timeline |
| `gainDb` | Track gain, -96 to +24 dB |
| `pan` | Stereo balance, -1 left to +1 right |
| `muted` | Excluded from the mix |
| `solo` | If any track is soloed, only solo tracks are audible |
| `armed` | Recording target |
| `empty` | Track has no source audio yet |

The active track is the target for editing, analysis, slicing, wavetable
extraction, and undo/redo. Click a track header or waveform lane to make it
active.

## Track controls

Every header has:

- `M` mute
- `S` solo
- `R` record arm
- `Vol` gain slider
- `Pan` balance slider
- `x` remove track

The header is highlighted with an accent bar for the active track. Muted or
solo-excluded tracks are dimmed in the waveform view. Track gain, pan, solo, and
mute affect playback live.

## Selection coordinates

The waveform and ruler use project timeline frames. Track documents use local
frames. `Timeline::setSelection` translates timeline ranges into each track's
local coordinates by subtracting that track's `startFrame`. This means an edit
applied to the active track affects the correct samples even when the track is
offset from zero.

## Mixing

`Timeline::mixdown()` renders a stereo mix:

- muted tracks are skipped;
- if any solo is active, non-solo tracks are skipped;
- track gain is applied in dB;
- mono tracks use equal-power panning; stereo tracks use balance;
- differing track sample rates are resampled to the project rate;
- track offsets and lengths are respected.

Playback uses the same model directly from immutable track buffers, so pressing
Play does not first render the entire project. Exporting mixdown uses
`Timeline::mixdown()`.

## Processing selections

Time-stretch and pitch-shift expose a **Target** selector:

- **Track** applies in place. The processed selection is spliced between the
  untouched prefix and suffix. Time-stretching changes the track length.
- **New Track** leaves the original untouched and adds the processed selection
  as a new track at the selection's timeline position.

**Edit → Selection to New Track** copies the raw selection as a new track.

## Project persistence

Timeline projects (`.vwdoc`) contain:

- every track's name, offset, gain, pan, mute, solo, arm, and empty state;
- each non-empty track's full source/edit history;
- the active track;
- the timeline selection.

Legacy single-document projects load as a one-track timeline. Audio-only export
writes the mixdown.
