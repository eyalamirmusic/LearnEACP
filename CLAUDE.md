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

- `CMakeLists.txt` — fetches EACP with CPM (`CMake/CPM.cmake`), includes
  `CMake/LearnEACP.cmake` and adds `Lib/`, `Tutorials/` and
  `Tools/ChapterRegistryCheck/`.
- `CMake/LearnEACP.cmake` — `learneacp_add_chapter()`, the one call in every
  chapter's `CMakeLists.txt`.
- `Lib/LearnEACP/Chapter.h` — the `learneacp-chapter` INTERFACE target:
  the `LEARNEACP_CHAPTER` / `LEARNEACP_CONSOLE_CHAPTER` macros and the chapter
  registry (`LearnEACP::registeredChapters()`).
- `Tutorials/CMakeLists.txt` — one `add_subdirectory` per chapter, in order.
- `Tutorials/NN-ChapterName/` — one chapter per directory.
- `Tools/ChapterRegistryCheck/` — a temporary console program that links every
  chapter's registry build into one binary, prints the chapters in order and
  exits 0 only if all of them registered. It proves the chapters link
  together; the planned Tour app replaces it.

## Chapter conventions

- Each chapter directory holds exactly three files: `README.md` (the tutorial
  text), `Main.cpp` (the one program the chapter builds) and `CMakeLists.txt`.
  Shared support code lives outside the chapter directories.
- A chapter's `CMakeLists.txt` is one call: `learneacp_add_chapter()`, or for
  `10-Compute`, the one console chapter (no window, keeps its console output
  on Windows, links the CPU executor),
  `learneacp_add_chapter(CONSOLE LIBRARIES eacp-cpu-compute)`. It derives the
  target from the directory name without its number prefix (`01-HelloWindow`
  builds `HelloWindow`), the number from the prefix and the title from the
  README's `# NN. Title` heading. It builds two things from `Main.cpp`: the
  standalone app (`eacp_add_app`, `MACOSX_BUNDLE`, `set_default_target_setting`,
  `eacp_set_gui_subsystem` unless `CONSOLE`), and `<Target>Chapter`, an object
  library compiled with `LEARNEACP_CHAPTER_REGISTRY` and appended to the global
  property `LEARNEACP_CHAPTER_LIBRARIES`.
- `Main.cpp` includes `<LearnEACP/Chapter.h>` after `<eacp/GPU/GPU.h>`, wraps
  all its code after the `using namespace` lines in a namespace named after the
  target (`namespace Textures`), and ends, outside that namespace, with
  `LEARNEACP_CHAPTER(Textures::TexturesView)` instead of `main()` (chapter 10:
  `LEARNEACP_CONSOLE_CHAPTER(Compute::runCompute)`). Standalone, the macro is
  `main()` calling `runWindowedApp` / `Apps::runConsole`; in the registry build
  it registers the chapter instead. No chapter may define names at global scope,
  so all of them link into one program.
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
`build/Tools/ChapterRegistryCheck/ChapterRegistryCheck.app/Contents/MacOS/ChapterRegistryCheck`
lists every chapter and exits with status 0 when all of them registered.
