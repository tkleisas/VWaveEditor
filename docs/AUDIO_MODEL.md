# Audio model: immutable blocks and regions

This document describes the target non-destructive audio model. The first
components are implemented in `AudioBlockStore` and `RegionArrangement`; the
document/timeline and editor migration follows in stages.

## Why

The editor must never modify source audio. Splitting a waveform must not copy
samples, and undo must not require keeping a complete track buffer for every
edit. The model therefore separates:

1. immutable audio content, addressed by `AudioBlockId`;
2. an arrangement of regions that reference content and add placement,
   stretch, gain, and fades;
3. a history that retains referenced blocks for as long as undo needs them.

A single imported WAV becomes one block and one region. A split creates two
regions referencing the same block. Moving or stretching a region changes
metadata only. A sample-changing operation creates a new block for the touched
range and leaves all other regions untouched.

## Invariants

- Block sample data is immutable after insertion.
- Block IDs are non-zero and stable within a project.
- A region always references an existing block.
- `sourceStart >= 0`, `sourceLength > 0`, and
  `sourceStart + sourceLength <= block.frames`.
- `timelineStart >= 0`; regions may overlap and gaps are silence.
- All blocks referenced by one region arrangement have the same sample rate
  and channel count.
- `stretch > 0`; output length is
  `round(sourceLength * stretch)`.
- Fades and gain are metadata and are applied during rendering.
- Rendering never mutates a block.

## Content store

`AudioBlockStore` owns immutable blocks. `get`/`tryGet` return a copy-on-write
`Audio`; mutating the returned value detaches it, so callers cannot corrupt the
stored block.

The store supports:

- monotonic block IDs inside a project;
- metadata: channels, frames, sample rate, checksum, byte size, name, file;
- a project-local backing directory with one raw float32 planar file per block
  and a `blocks.json` manifest;
- an LRU cache with a byte budget. Persisted blocks can be evicted and loaded
  again transparently;
- checksum verification whenever a block is loaded from disk;
- atomic block-file replacement and an atomically replaced manifest;
- validation of unsafe manifest paths and truncated files.

Raw work files are an internal cache/project store, not the interchange format.
Final export remains WAV/AIFF/FLAC/Ogg through `AudioIO`.

The store intentionally does not own reference counting. The arrangement and
history own references; garbage collection will retain blocks reachable from
the current state and the undo/redo graph.

## Region arrangement

`RegionArrangement` owns the ordered region list for one track and implements
the metadata operations:

- initial full region;
- split one region, or every region crossing a frame, preserving source
  mapping for stretched regions;
- remove, move, stretch, repeat;
- erase, insert, replace, and crop timeline ranges with ripple behaviour;
- gain and fades;
- overlap mixing and gap rendering;
- validation against the block store;
- JSON round-trip using block IDs.

A split is O(1) in sample data: it copies one region record and changes source
offsets. Render is the only operation that materializes audio, and the result is
cached by the document/track layer.

## Sample-changing edits

A future document operation maps a timeline range to an arrangement change:

1. split regions at the range boundaries;
2. render only the selected range from the arrangement;
3. apply the existing DSP operation to that slice;
4. insert the result as a new block and replace the covered regions with one
   region over it;
5. preserve regions and content outside the edit;
6. record the before/after arrangement metadata in history.

This keeps the untouched regions non-destructive and bounds new audio to the
edited range. Length-changing edits (delete, insert, replace, crop, repeat)
apply the same principle while shifting or trimming later region metadata.
Operations that are inherently whole-track (resample, replace-all) create one
new block/region and retain the previous blocks for undo.

## Persistence and recovery

A timeline project writes a sidecar work area next to the project file:

```text
project.vwdoc
project.vwdoc.blocks/
  track-0-<generation>/
    blocks.json
    1.f32, 2.f32, ...
```

Each save writes new generation directories containing only blocks reachable
from the current arrangement, history, and redo graph. The project file is
written last and points at those directories, so a failed save leaves the
previous project and blocks intact. Unreachable blocks are not written into a
new generation.

Old generations are retained while the session is open because timeline
structural undo snapshots can still reference their blocks. Loading a project
removes every generation directory not referenced by the loaded project file,
which is safe because in-memory undo history is cleared on load. Unreferenced
blocks therefore disappear on the next project load.

The in-memory LRU cache is separate from persistence: evicting a block only
drops its decoded copy. Loaded timeline tracks use a 256 MiB default cache limit
and reload evicted blocks on demand. Playback/analysis consumers must hold an
`Audio` reference for as long as they use it.

The in-memory LRU cache is separate from persistence: evicting a block only
drops its decoded copy. Playback/analysis consumers must hold an `Audio`
reference for as long as they use it.

## Threading

- Blocks are immutable; copies share storage.
- The store mutex protects metadata and cache state only.
- Audio callbacks and DSP workers must never perform blocking disk I/O. The
  project layer prefetches required blocks into RAM before playback/rendering.
- Disk writes happen on the main thread or a dedicated I/O worker, never in the
  audio callback.

## Migration stages

1. **Core model** (done): `AudioBlockStore` and `RegionArrangement` with
   persistence, checksums, cache eviction, split/move/stretch/repeat/render
   and tests.
2. **Document integration** (done): documents always own one region
   arrangement; import creates a full region; sample edits use range-local
   derived blocks; history stores arrangement snapshots plus block references.
3. **Project format v2** (done): v2 projects serialize arrangements and
   history snapshots, plus reachable blocks either inline or in a generation-
   based work-area sidecar. Legacy v1 projects migrate by replaying operations
   into the region model.
4. **Editor integration** (done for Make Clips): the editor no longer creates a
   clip mode. Region boundaries are always visible, region operations are
   ordinary track edits, and sample edits no longer require flattening.
5. **Durability and scale** (in progress): bounded document history releases
   unreferenced blocks; background prefetch/write-back, autosave, and crash
   recovery testing remain.

## Tests

`tests/test_audio_block_store.cpp` covers copy-on-write storage, persistence,
cache eviction, corruption, unsafe manifests, and metadata serialization.

`tests/test_region_arrangement.cpp` covers reference-based splits, stretched
source mapping, gaps/overlaps, ripple repeat, range erase/insert/replace/crop,
gain/fades, removal, validation, format consistency, and JSON round-trips.
