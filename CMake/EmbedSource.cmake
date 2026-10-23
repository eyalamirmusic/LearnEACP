# cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DNAME=<Name> -DDISPLAY_PATH=<path>
#       -P EmbedSource.cmake
#
# Writes OUTPUT, a C++ file defining LearnEACP::EmbeddedSources::<NAME>Source:
# the text of INPUT as a LearnEACP::ChapterSource, one string_view literal per
# line. Lines rather than one literal because compilers cap a literal's length
# (MSVC at 16 KB), and lines are what a code view shows anyway.
#
# Each line is a raw string literal, so nothing in it needs escaping. The one
# thing it cannot hold is its own closing delimiter, which is checked for.
# learneacp_embed_chapter_sources() runs this as a build step, so OUTPUT is
# regenerated whenever INPUT changes.
foreach (variable INPUT OUTPUT NAME DISPLAY_PATH)
    if (NOT DEFINED ${variable})
        message(FATAL_ERROR "EmbedSource.cmake: ${variable} is not set")
    endif ()
endforeach ()

file(READ "${INPUT}" content)
string(REPLACE "\r" "" content "${content}")

set(delimiter "learneacp")
set(lines "")

# Walked with FIND rather than turned into a CMake list: C++ is full of the
# semicolons and square brackets a list would split on or swallow.
string(LENGTH "${content}" remaining)

while (remaining GREATER 0)
    string(FIND "${content}" "\n" newline)

    if (newline EQUAL -1)
        set(line "${content}")
        set(content "")
    else ()
        string(SUBSTRING "${content}" 0 ${newline} line)
        math(EXPR next "${newline} + 1")
        string(SUBSTRING "${content}" ${next} -1 content)
    endif ()

    string(FIND "${line}" ")${delimiter}\"" clash)

    if (NOT clash EQUAL -1)
        message(FATAL_ERROR "EmbedSource.cmake: ${INPUT} contains )${delimiter}\"")
    endif ()

    string(APPEND lines "    R\"${delimiter}(${line})${delimiter}\",\n")
    string(LENGTH "${content}" remaining)
endwhile ()

file(WRITE "${OUTPUT}" "// Generated from ${DISPLAY_PATH} by CMake/EmbedSource.cmake. Do not edit.
#include <LearnEACP/ChapterSource.h>

namespace LearnEACP::EmbeddedSources
{
namespace
{
constexpr std::string_view lines[] = {
${lines}};
} // namespace

extern const ChapterSource ${NAME}Source;
const ChapterSource ${NAME}Source {\"${DISPLAY_PATH}\", lines};
} // namespace LearnEACP::EmbeddedSources
")
