#!/usr/bin/env python3
"""Check that every C++ snippet in a chapter README matches the chapter's Main.cpp.

    python3 Tools/check_snippets.py                       # every chapter
    python3 Tools/check_snippets.py Tutorials/04-Uniforms # one or more chapters
    python3 Tools/check_snippets.py --list                # also print each match

Main.cpp is the source of truth. A ```cpp fence in a chapter's README.md passes
when its lines appear in that chapter's Main.cpp, in order, as one contiguous
block, with these allowances:

- trailing whitespace is ignored, and so are blank lines at the start and end
  of the snippet;
- the snippet may be indented more or less than the source, as long as the
  difference is the same for every line of it (of each part, see below);
- comment-only source lines may be left out of the snippet, together with
  blank lines next to them: the prose usually says what the comments say.
  Code lines may not be left out, and comments the snippet does show must
  match;
- an elision line, one that is only `...` or a comment starting with `...`
  (`// ...`, `// ... the corners v0, v1, v2 ...`) at any indentation, stands
  for any number of skipped source lines, including none. It splits the
  snippet into parts that must appear in order, each with its own indent.

A snippet that deliberately does not come from Main.cpp (a simplified version,
a "don't do this", the code a macro stands for) is marked illustrative by an
HTML comment on the line directly above its fence (blank lines between are
allowed):

    <!-- snippet: illustrative -->
    ```cpp

Text after the word, before `-->`, is free for a reason. Fences in any other
language (```metal, bare ``` for diagrams and program output) are not checked.

The matching code is importable, for tools such as a site generator that want
to link a snippet to the source lines it came from: see snippets_for_chapter()
and match_snippet().

Exits with status 1 if any snippet fails, 0 otherwise. Standard library only.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent
TUTORIALS = ROOT / "Tutorials"

CHECKED_LANGUAGES = {"cpp", "c++"}

ILLUSTRATIVE_MARKER = re.compile(r"^\s*<!--\s*snippet:\s*illustrative\b.*-->\s*$")
FENCE_OPEN = re.compile(r"^(?P<indent> {0,3})(?P<fence>`{3,}|~{3,})(?P<info>.*)$")
ELISION = re.compile(r"^(\.\.\.|//\s*\.\.\..*)$")

# Statuses a snippet can end up with.
MATCH = "match"
MISMATCH = "mismatch"
ILLUSTRATIVE = "illustrative"
UNCHECKED = "unchecked"  # a fence in a language that is not checked

# A 1-based, inclusive range of source lines.
LineRange = Tuple[int, int]


@dataclass
class Snippet:
    """One fenced code block in a README."""

    readme: Path
    fence_line: int  # 1-based line of the opening fence
    language: str  # first word of the info string, lower case, may be ""
    lines: List[str]  # the block's content, fence indentation removed
    illustrative: bool = False


@dataclass
class SnippetResult:
    snippet: Snippet
    status: str
    source: Optional[Path] = None
    # Where a matching snippet came from: one range per elision-separated part.
    ranges: List[LineRange] = field(default_factory=list)
    # For a mismatch: the closest source region and a diff against it.
    closest: Optional[LineRange] = None
    diff: str = ""


# --- Parsing -----------------------------------------------------------------


def parse_snippets(readme: Path) -> List[Snippet]:
    """Every fenced code block in a Markdown file, in order."""
    text = readme.read_text(encoding="utf-8").splitlines()
    snippets: List[Snippet] = []
    i = 0

    while i < len(text):
        opening = FENCE_OPEN.match(text[i])

        if not opening:
            i += 1
            continue

        indent = len(opening.group("indent"))
        fence = opening.group("fence")
        info = opening.group("info").strip()
        language = info.split()[0].lower() if info else ""

        # The marker sits on the nearest non-blank line above the fence.
        above = i - 1
        while above >= 0 and not text[above].strip():
            above -= 1
        illustrative = above >= 0 and bool(ILLUSTRATIVE_MARKER.match(text[above]))

        closing = re.compile(r"^ {0,3}" + re.escape(fence[0]) + "{" + str(len(fence)) + r",}\s*$")
        body: List[str] = []
        j = i + 1

        while j < len(text) and not closing.match(text[j]):
            line = text[j]
            # Content loses up to the fence's own indentation, as in CommonMark.
            strip = min(indent, len(line) - len(line.lstrip(" ")))
            body.append(line[strip:])
            j += 1

        snippets.append(Snippet(readme, i + 1, language, body, illustrative))
        i = j + 1

    return snippets


# --- Matching ----------------------------------------------------------------


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" \t"))


def _segments(lines: Sequence[str]) -> List[List[str]]:
    """The snippet split at elision lines, with blank edges trimmed."""
    segments: List[List[str]] = [[]]

    for line in lines:
        line = line.rstrip()
        if ELISION.match(line.strip()):
            segments.append([])
        else:
            segments[-1].append(line)

    trimmed = []
    for segment in segments:
        while segment and not segment[0]:
            segment.pop(0)
        while segment and not segment[-1]:
            segment.pop()
        if segment:
            trimmed.append(segment)

    return trimmed


def _is_comment(line: str) -> bool:
    return line.lstrip(" \t").startswith("//")


def _skippable(source: List[str], j: int) -> bool:
    """Whether a snippet may leave out source line j: a comment-only line, or a
    blank line next to one (the spacing that goes with a left-out comment)."""
    if _is_comment(source[j]):
        return True
    if source[j]:
        return False
    return (j > 0 and _is_comment(source[j - 1])) or (j + 1 < len(source) and _is_comment(source[j + 1]))


def _lines_match(wanted: str, actual: str, delta: Optional[int]) -> Tuple[bool, Optional[int]]:
    """Whether one snippet line matches one source line; returns the indent delta used."""
    if not wanted:
        return not actual, delta

    if actual.lstrip(" \t") != wanted.lstrip(" \t"):
        return False, delta

    shift = _indent(actual) - _indent(wanted)
    if delta is not None and shift != delta:
        return False, delta

    return True, shift


def _match_at(segment: List[str], source: List[str], start: int, delta: Optional[int]):
    """Match segment against source starting exactly at index start.

    Returns (end, delta): the index one past the last source line used and the
    indent delta, or None. Comment-only source lines inside the block may be
    left out of the snippet.
    """
    # Depth-first over (snippet line, source line); skipping a comment is only
    # tried when matching the line as it stands fails further on.
    def walk(i: int, j: int, delta: Optional[int]):
        if i == len(segment):
            return j, delta
        if j == len(source):
            return None

        ok, used = _lines_match(segment[i], source[j], delta)
        if ok:
            found = walk(i + 1, j + 1, used)
            if found is not None:
                return found

        # The first line of a part is where it starts, so it is never skipped.
        if i > 0 and _skippable(source, j):
            return walk(i, j + 1, delta)

        return None

    return walk(0, start, delta)


def match_snippet(lines: Sequence[str], source_lines: Sequence[str]) -> Optional[List[LineRange]]:
    """Locate a snippet in the source.

    Returns one 1-based inclusive line range per elision-separated part of the
    snippet, or None if it does not match. An empty snippet matches nothing.
    """
    source = [line.rstrip() for line in source_lines]
    segments = _segments(lines)

    if not segments:
        return None

    def search(index: int, start: int) -> Optional[List[LineRange]]:
        if index == len(segments):
            return []

        segment = segments[index]
        # The first part may be anywhere; later parts anywhere after the last.
        # Each part finds its own indent shift, since parts often come from
        # different scopes.
        for position in range(start, len(source) - len(segment) + 1):
            found = _match_at(segment, source, position, None)
            if found is None:
                continue
            end, _ = found
            rest = search(index + 1, end)
            if rest is not None:
                return [(position + 1, end)] + rest

        return None

    return search(0, 0)


def closest_region(lines: Sequence[str], source_lines: Sequence[str]) -> Tuple[Optional[LineRange], str]:
    """The source region most like the snippet, and a unified diff against it."""
    wanted = [line.rstrip() for line in lines if not ELISION.match(line.strip())]
    while wanted and not wanted[0]:
        wanted.pop(0)
    while wanted and not wanted[-1]:
        wanted.pop()

    source = [line.rstrip() for line in source_lines]
    stripped_wanted = [line.strip() for line in wanted]
    stripped_source = [line.strip() for line in source]
    window = max(len(wanted) + len(wanted) // 2 + 5, 10)

    best: Optional[Tuple[int, int, int]] = None  # (score, first, last), 0-based
    candidates = {line for line in stripped_wanted if line}

    for start in range(len(source)):
        if stripped_source[start] not in candidates:
            continue
        chunk = stripped_source[start : start + window]
        matcher = difflib.SequenceMatcher(None, stripped_wanted, chunk, autojunk=False)
        blocks = [block for block in matcher.get_matching_blocks() if block.size]
        score = sum(block.size for block in blocks)
        if blocks and (best is None or score > best[0]):
            last = blocks[-1].b + blocks[-1].size - 1
            best = (score, start, start + last)

    if best is None:
        return None, "  (no line of the snippet appears in the source)\n"

    _, first, last = best
    # Comments the snippet leaves out are allowed, so keep them out of the diff.
    shown = set(stripped_wanted)
    region = [line for line in source[first : last + 1] if not _is_comment(line) or line.strip() in shown]

    # Show both sides at their own minimum indentation, so an indent shift
    # alone does not flood the diff.
    def dedent(block: List[str]) -> List[str]:
        widths = [_indent(line) for line in block if line]
        cut = min(widths) if widths else 0
        return [line[cut:] for line in block]

    diff = difflib.unified_diff(dedent(wanted), dedent(region), "README", "Main.cpp", lineterm="", n=2)
    text = "".join("  " + line + "\n" for line in list(diff)[2:])
    return (first + 1, last + 1), text


def check_snippet(snippet: Snippet, source: Path, source_lines: Sequence[str]) -> SnippetResult:
    if snippet.language not in CHECKED_LANGUAGES:
        return SnippetResult(snippet, UNCHECKED, source)

    if snippet.illustrative:
        return SnippetResult(snippet, ILLUSTRATIVE, source)

    ranges = match_snippet(snippet.lines, source_lines)
    if ranges is not None:
        return SnippetResult(snippet, MATCH, source, ranges)

    closest, diff = closest_region(snippet.lines, source_lines)
    return SnippetResult(snippet, MISMATCH, source, closest=closest, diff=diff)


def snippets_for_chapter(chapter: Path) -> List[SnippetResult]:
    """Every fenced block in chapter/README.md, checked against chapter/Main.cpp."""
    readme = chapter / "README.md"
    source = chapter / "Main.cpp"
    source_lines = source.read_text(encoding="utf-8").splitlines()
    return [check_snippet(snippet, source, source_lines) for snippet in parse_snippets(readme)]


def chapters() -> List[Path]:
    return sorted(path for path in TUTORIALS.iterdir() if (path / "README.md").is_file() and (path / "Main.cpp").is_file())


# --- Command line ------------------------------------------------------------


def _relative(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Check README snippets against each chapter's Main.cpp.")
    parser.add_argument("chapters", nargs="*", type=Path, help="chapter directories (default: all)")
    parser.add_argument("--list", action="store_true", help="also print where each snippet matched")
    arguments = parser.parse_args(argv)

    selected = arguments.chapters or chapters()
    totals = {MATCH: 0, MISMATCH: 0, ILLUSTRATIVE: 0}

    for chapter in selected:
        chapter = chapter.resolve()
        if not (chapter / "README.md").is_file() or not (chapter / "Main.cpp").is_file():
            print(f"error: {_relative(chapter)} has no README.md and Main.cpp", file=sys.stderr)
            return 2

        for result in snippets_for_chapter(chapter):
            if result.status == UNCHECKED:
                continue
            totals[result.status] += 1
            where = f"{_relative(result.snippet.readme)}:{result.snippet.fence_line}"
            source = _relative(result.source) if result.source else "Main.cpp"

            if result.status == MISMATCH:
                print(f"{where}: snippet does not match {source}")
                if result.closest:
                    print(f"  closest: {source}:{result.closest[0]}-{result.closest[1]}")
                print(result.diff, end="")
                print()
            elif arguments.list:
                if result.status == MATCH:
                    spans = ", ".join(f"{a}-{b}" for a, b in result.ranges)
                    print(f"{where}: {source}:{spans}")
                else:
                    print(f"{where}: illustrative")

    print(
        f"{totals[MATCH]} snippets match, {totals[ILLUSTRATIVE]} illustrative, "
        f"{totals[MISMATCH]} do not match"
    )

    if totals[MISMATCH]:
        print(
            "Make each failing snippet match Main.cpp, or mark it with "
            "<!-- snippet: illustrative --> on the line above its fence."
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
