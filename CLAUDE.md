# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git Rules

Claude must never commit or push without explicit permission from the user in
the current conversation.

Claude must never appear in commit metadata: not as author or committer, and
not as a `Co-Authored-By` trailer or a "Generated with Claude" line in the
message. The hooks under `.githooks/` refuse such a commit; activate them once
per clone with `git config core.hooksPath .githooks`.

## Project Overview

LearnEACP is a learnopengl.com-style tutorial series teaching graphics
programming through EACP's shader EDSL and GPU module. `README.md` is written
for readers of the series and stays free of build-system and implementation
detail.

## Layout

- `CMakeLists.txt` — fetches EACP with CPM (`CMake/CPM.cmake`) and adds
  `Tutorials/`.
- `Tutorials/CMakeLists.txt` — one `add_subdirectory` per chapter, in order.
- `Tutorials/NN-ChapterName/` — one chapter per directory.

## Chapter conventions

- Each chapter directory holds exactly three files: `README.md` (the tutorial
  text), `Main.cpp` (the one program the chapter builds) and `CMakeLists.txt`.
- The CMake target is the directory name without its number prefix
  (`01-HelloWindow` builds `HelloWindow`). Each chapter's `CMakeLists.txt` uses
  `eacp_add_app`, links `eacp-gpu`, sets `MACOSX_BUNDLE` and calls
  `set_default_target_setting`. A windowed chapter also calls
  `eacp_set_gui_subsystem`; a console chapter omits it so it keeps its console
  output on Windows. `10-Compute` is the one console chapter: it opens no
  window, and it also links `eacp-cpu-compute` for the CPU executor.
- Code follows `.clang-format`; run `clang-format -i` on every source file
  touched.
- EACP is fetched by CPM at configure time (`GIT_TAG main`, `NO_CACHE`, so it
  is always a fresh clone) and lives under `build/_deps/eacp-src` after a
  configure. Look up its API there — start with
  `build/_deps/eacp-src/Lib/eacp/GPU/README.md` and the examples under
  `build/_deps/eacp-src/Apps/GPU/`. Never modify it.

## Build

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

`-DCPM_SOURCE_CACHE=$HOME/.cache/CPM` avoids re-downloading EACP's
dependencies, but the cache never refreshes a `main`-tracking package: a stale
entry shows up as a missing header inside one of them (`ea_data_structures`,
`Miro`). Delete that entry under `~/.cache/CPM`, or configure without the
cache. EACP itself is `NO_CACHE`.

On macOS a chapter runs as
`build/Tutorials/<dir>/<Target>.app/Contents/MacOS/<Target>`. Windowed
chapters run until closed; `Compute` (chapter 10) prints its results and exits
with status 0 when the GPU and CPU results match.
