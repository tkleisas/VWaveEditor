# Notices

VWaveEditor is MIT licensed. It uses the following third-party components and
reuses design/algorithmic work from YAWN.

## Vendored

- **Lua 5.4** — Copyright (C) 1994-2024 Lua.org, PUC-Rio. MIT license.
  Sources in `third_party/lua54`.
- **nlohmann/json** — Copyright (C) 2013-2023 Niels Lohmann. MIT license.
  Header in `third_party/nlohmann/json.hpp`.

## Linked

- **libsndfile** — LGPL-2.1-or-later. Linked as a system shared library.
- **SDL2** and **SDL2_ttf** (standalone editor) — zlib license.

## YAWN reuse

VWaveEditor and YAWN are both MIT projects by Tasos Kleisas. The time-stretch
and transient/onset DSP designs were adapted from YAWN's `TimeStretcher` and
`TransientDetector`. The standalone editor adopts YAWN's dark editor visual
language and interaction patterns, but uses an independent SDL2 frontend and
does not link `yawn_core`.

Full license texts are distributed with the respective system packages.
