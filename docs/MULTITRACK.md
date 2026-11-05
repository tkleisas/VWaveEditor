# Multi-track timeline

VWaveEditor's desktop editor is built on `vwave::Timeline`. Each track owns an
independent non-destructive `vwave::Document` plus playback/recording metadata.

## Track model

| Property | Meaning |
| --- | --- |
| `name` | Display name |
| `document` | Non-destructive source/history for the track audio |
| `channels` | Mono or stereo track audio |
| `startFrame` | Placement on the project timeline |
| `gainDb` | Track gain, -96 to +24 dB |
| `pan` | Stereo balance, -1 left to +1 right |
| `muted` | Excluded from the mix |
| `solo` | If any track is soloed, only solo tracks are audible |
| `armed` | Recording target |
| `empty` | Track has no source audio yet |

The active track is the target for editing, analysis, slicing, wavetable
extraction, and undo/redo. Click a track header or waveform lane to make it
active. Mastering analysis is the exception: it analyzes all audible stem
tracks together, using their timeline offsets for alignment.

Selection has an explicit scope:

- **All Tracks** — the timeline range is applied to every track at its local
  offset. This is the original behavior.
- **Active Track** — the timeline range is applied only to the active track;
  other tracks have no document selection.

Toggle the scope in the **Track** sidebar section or from the track context
menu. The scope persists in timeline projects.

## Track controls and context menu

Right-click a track header or lane for:

- Select active track only / select all tracks
- Zero-cross cut or snap the selection to zero crossings
- Copy or move the selection to the playhead
- Repeat the selection
- Crop track to selection
- Shift track to the playhead
- Shift track left/right by 1 second
- Stretch the whole track in place
- Split stereo to mono tracks
- Combine with the previous or next mono track
- Delete track

The sidebar **Track** section mirrors selection scope, crop, editing, and
1-second shifts. **Alt+drag** a header or lane for arbitrary horizontal
shifting.

Cropping a track keeps the retained audio at its original timeline position by
updating the track start offset. The crop itself is a document operation;
start-offset changes are track metadata and are not part of document undo
history. Shifting a track never changes its audio. The trim handles at the
start and end of a track use the same crop operation.

## Range editing

Selection handles appear on the left and right edges of an active selection.
Drag either edge to adjust it. The small orange square at the lower-right edge
repeats the selection: drag right to add whole copies after the current range.
Repeat is a ripple-insert operation, so following audio moves later. Undo
restores the previous length and samples.

Move and copy are undoable document operations, not clip operations. A move is
one history entry; destination coordinates refer to the document before the
refactor. The destination cannot be inside the moved range.

Whole-track stretch ignores the selection. Drag the orange handle at the
upper-right of a track, or use **Stretch Track**, to apply the current algorithm
and ratio. The selection's stretch controls still operate on the selected
range.

Zero-cross snapping searches a short window around each selection edge. The
**Zero-X Cut** action snaps both edges and deletes the range in one undoable
step.

## Region arrangement

Every track is always a region arrangement. Loading a file creates one
full-length region over an immutable audio block; there is no **Make Clips**
step. A split creates two regions that reference the same block without copying
samples.

Operations:

- Enable **Region Tool**, click a region to select it, then drag the body to
  move it or drag the orange right-edge handle to stretch it. Region boundaries
  are visible even when the tool is off.
- **Split Region** splits at the playhead. The right part becomes a new region.
- **Delete Region** removes the region and leaves a gap.
- **Repeat Region x2** inserts additional copies after the region and ripples
  later regions.
- **Move to Head** moves the selected region to the playhead.
- **Stretch Region** applies the current stretch ratio to the selected region.
- **Flatten Regions** renders the arrangement into one consolidated region and
  drops the region boundaries. Old blocks remain for undo.

Sample edits remain available while regions exist. Gain, fades, reverse, cut,
delete, paste, spectral processing, and other range edits render only the
selected range into a new immutable block and replace the covered region range.
Audio outside the edit is untouched; baked overlaps inside the selection become
one region.

Region operations and sample edits share the active track's undo/redo stack.
Undo returns the previous arrangement and selection; blocks referenced by
history are retained until the history is cleared.

Automation can set gain and fades via `set_clip_gain` and `set_clip_fades`.
The legacy `make_clips` command is now an idempotent compatibility no-op.

## Mono and stereo tracks

Selection and edit operations are track-level. A stereo edit affects both
channels. To process channels independently, split the stereo track:

- **Split Stereo** creates `Name L` and `Name R` mono tracks at the same
  timeline position.
- The new tracks are hard-panned left/right and their gains preserve the
  original stereo balance.
- The original track is muted and retained by default.

Combine two mono tracks with **Merge Previous** or **Merge Next**:

- A new stereo track is created with the same start as the earlier source.
- The first track supplies the left channel and the second supplies the right.
- Track offsets are preserved as front padding.
- The shorter channel is padded at the end by default. The **Pad** selector
  switches to front or both-end padding.
- Source tracks are muted and retained by default.
- The combined track ignores source pan (the two sources are already mapped to
  left/right); source gain is applied.

The desktop merge actions combine the active mono track with its previous or
next neighbor. Automation can combine any two mono tracks by index. The CLI
exposes the same operations as `vwave project split-stereo` and
`vwave project combine-mono`. Session/Lua/MCP commands are
`split_stereo_track` and `combine_mono_tracks`.

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

## Playhead and transport

The playhead can be moved with the transport controls:

- `|<` and `>|` jump to the beginning and end.
- The time display in the toolbar shows the exact playhead position.
- Clicking the time display opens an exact-time entry dialog.
- `Home` / `End` jump to the beginning / end; `G` opens exact-time entry.
- Entry accepts seconds or `MM:SS.mmm` / `HH:MM:SS.mmm`.
- Clicking the waveform, 2D spectrum, ruler, or overview positions the playhead.
- Playback without a selection starts at the playhead instead of always
  starting at zero.

### Loop playback

- Shift+drag in the waveform, 2D spectrum, or ruler creates a loop region.
- **Loop Selection** sets the loop from the current timeline selection.
- **Loop: On/Off** (`L`) enables or disables looping; `Shift+L` loops the
  current selection.
- **Clear Loop** removes the region.
- When loop playback is enabled, playback stays inside the region and wraps at
  the loop end. If the playhead is outside the region, playback starts at the
  loop start.
- The loop region is session state; it is not saved in the project.

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

In-place sample processing and selection moves are rejected while a clip
arrangement exists. Flatten the clips first; then the arranged audio becomes the
track base and destructive editing resumes. Processing to a new track is still
available because it reads the arranged output.

## Project persistence

Timeline projects (`.vwdoc`) contain:

- every track's name, offset, gain, pan, mute, solo, arm, and empty state;
- each non-empty track's full source/edit history, including clip arrangements;
- the active track;
- the timeline selection.

Legacy single-document projects load as a one-track timeline. Audio-only export
writes the mixdown.
