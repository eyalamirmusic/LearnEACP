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

- `CMakeLists.txt` — fetches EACP and then ECode with CPM (`CMake/CPM.cmake`),
  includes `CMake/LearnEACP.cmake` and adds `Lib/`, `Tutorials/` and
  `Apps/Tour/`.
- `CMake/LearnEACP.cmake` — `learneacp_add_chapter()`, the one call in every
  chapter's `CMakeLists.txt`, and `learneacp_embed_chapter_sources(<target>)`,
  which compiles every chapter's `Main.cpp` into a target as data.
- `CMake/EmbedSource.cmake` — the script-mode build step behind it: one
  `Main.cpp` to a `.cpp` holding its lines as raw string literals.
- `Lib/LearnEACP/Chapter.h` — the `learneacp-chapter` INTERFACE target:
  the `LEARNEACP_CHAPTER` / `LEARNEACP_CONSOLE_CHAPTER` macros and the chapter
  registry (`LearnEACP::registeredChapters()`).
- `Lib/LearnEACP/ChapterSource.h` — `LearnEACP::ChapterSource` (path and lines)
  and `findChapterSource(name)`, defined by the generated code.
- `Tutorials/CMakeLists.txt` — one `add_subdirectory` per chapter, in order.
- `Tutorials/NN-ChapterName/` — one chapter per directory.
- `Apps/Tour/` — the `Tour` app: every chapter in one window (see below).
- `Tools/check_snippets.py` — checks README code snippets against `Main.cpp`
  (see "README snippets" below). Python 3, standard library only.

## The Tour

`Tour` links every library in `LEARNEACP_CHAPTER_LIBRARIES` (so it is added
after `Tutorials/`) and embeds every `Main.cpp`; a changed `Main.cpp` is
re-embedded on the next build. Its chrome is ECode widgets drawn in GPUViews
(`WidgetView`, after shadertoyeacp's Gallery), and the code pane is ECode's
read-only `CodeEditorView` with the tree-sitter C++ `SyntaxHighlighter`.

- `Tour.{h,cpp}` — `App` (chapter switching, menu bar, screenshot),
  `Layout` (toolbar, sidebar, code and result per layout mode) and
  `ResultArea`, the only owner of the chapter's view: switching builds the new
  view, swaps it in and destroys the old one, then focuses it.
- `Chrome.{h,cpp}` — `Button`, `Header`, `Toolbar`, `Sidebar`.
- `Panes.{h,cpp}` — `CodePane`, `ConsolePane` and `runCapturingOutput`, which
  runs a console chapter with stdout/stderr redirected (dup2 to a temporary
  file) and echoes what it printed afterwards.
- Chapters keep plain keys; host shortcuts are menu items with Cmd: Cmd+1..4
  layouts (result, side by side, stacked, code), Cmd+[ / Cmd+] previous/next
  chapter, Cmd+B chapter list, Cmd+R reload (runs chapter 10 again).
- Panes that are not shown are removed from the view tree, not hidden:
  `View::renderToImage` draws hidden views.

ECode (github.com/eyalamirmusic/ECode, `GIT_TAG main`, `NO_CACHE`) lives under
`build/_deps/ecode-src`. It fetches EACP under the same CPM name, so its
`CPMAddPackage` must stay after EACP's: CPM then reuses EACP's checkout and the
first call's options win.

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

## README snippets

`Main.cpp` is the source of truth, and it carries no markers for the README:
readers read it directly and in the Tour. Every ` ```cpp ` fence in a chapter's
README must match that chapter's `Main.cpp` or be marked illustrative. When
writing or editing a README, or changing a `Main.cpp`, run
`python3 Tools/check_snippets.py` (or `cmake --build build --target
check-snippets`) and make it pass; it exits non-zero and prints the README
line, the closest `Main.cpp` lines and a diff for each failure.

- A snippet matches when its lines appear in `Main.cpp` in order as one
  contiguous block. Trailing whitespace and a uniform indent shift are ignored;
  comment-only source lines (and blank lines beside them) may be left out, but
  code lines may not, and comments the snippet shows must match.
- A line that is only `...` or a comment starting with `...` (`// ...`,
  `// ... the corners v0, v1, v2 ...`) elides any number of source lines.
- Copy wrapping exactly as clang-format left it in `Main.cpp`; do not re-wrap
  or abbreviate inside a line (`x = ...;` fails).
- A snippet deliberately not from `Main.cpp` (another chapter's code, a
  simplified or "don't do this" version, the hand-written `main()` the macro
  stands for, an exercise's solution) gets `<!-- snippet: illustrative -->` on
  the line directly above its fence, at the fence's indentation. Never change
  `Main.cpp` to make a snippet pass; fix the snippet, then the prose around it.
- Only ` ```cpp ` fences are checked: ` ```metal ` and bare fences (diagrams,
  program output) are not.
- Names the prose tells the reader to change ("set `useSlopeScaledBias` to
  `false`") must exist in that chapter's `Main.cpp`; the checker does not test
  this.

The module is importable (`snippets_for_chapter(path)` returns each fence with
its status and `Main.cpp` line ranges), for a site generator to link snippets
to source lines.

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

The Tour runs as `build/Apps/Tour/Tour.app/Contents/MacOS/Tour`:

- `--list` prints every chapter and exits with status 0 only when all of them
  registered, in order, each with one entry point and an embedded source.
- `--chapter <N|Name>` (default 1), `--layout result|side-by-side|stacked|code`
  (default side-by-side) and `--no-sidebar` choose how it opens.
- `--screenshot <path.png> [--after <seconds>]` opens the window, waits
  (default 2 s, then until the code is highlighted, up to 5 s more), renders the
  whole window to a PNG with `View::renderToImage` and exits 0, or 1 on failure.
  Bad arguments exit 2.
