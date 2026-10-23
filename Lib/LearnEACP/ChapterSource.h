#pragma once

// A chapter's Main.cpp, embedded in the program that shows it.
//
// A program that wants the listings calls learneacp_embed_chapter_sources() on
// its target in CMake. Every chapter's Main.cpp is then turned into data at
// build time, and regenerated whenever the file changes, so the program reads
// no files at run time and always shows the code it was built from.

#include <span>
#include <string>
#include <string_view>

namespace LearnEACP
{
struct ChapterSource
{
    // From the repository root, e.g. "Tutorials/01-HelloWindow/Main.cpp".
    std::string_view path;

    // The file's lines, without their line endings.
    std::span<const std::string_view> lines;

    // The whole file, each line ending in '\n'.
    std::string text() const
    {
        auto result = std::string {};

        for (auto line: lines)
        {
            result.append(line);
            result += '\n';
        }

        return result;
    }
};

// The source of the chapter whose target is `name` ("HelloWindow"), or null for
// a name that is not a chapter. Defined by learneacp_embed_chapter_sources().
const ChapterSource* findChapterSource(std::string_view name);
} // namespace LearnEACP
