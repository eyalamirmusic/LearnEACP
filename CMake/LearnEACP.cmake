# learneacp_add_chapter([CONSOLE] [LIBRARIES <target>...])
#
# Called from a chapter's CMakeLists.txt, in a directory named NN-Name. Builds
# the chapter twice from its Main.cpp:
#
# - <Name>, the standalone app, where LEARNEACP_CHAPTER(View) is main(). A
#   macOS bundle with EACP's default settings, and a GUI app on Windows unless
#   CONSOLE is given (a console chapter keeps its console output there).
# - <Name>Chapter, an object library compiled with LEARNEACP_CHAPTER_REGISTRY,
#   where the same macro registers the chapter instead. Its name is appended to
#   the global property LEARNEACP_CHAPTER_LIBRARIES, in chapter order, so a
#   program that links them all can list every chapter. Linking the object
#   files directly (rather than through a static library) keeps every
#   registration in the final program.
#
# LIBRARIES are linked into both, on top of eacp-gpu.
function(learneacp_add_chapter)
    cmake_parse_arguments(CHAPTER "CONSOLE" "" "LIBRARIES" ${ARGN})

    get_filename_component(directory "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

    if (NOT directory MATCHES "^([0-9]+)-(.+)$")
        message(FATAL_ERROR "learneacp_add_chapter: '${directory}' is not NN-Name")
    endif ()

    math(EXPR number "${CMAKE_MATCH_1}")
    set(target "${CMAKE_MATCH_2}")

    # The title is the README's heading without its number: "# 01. Hello Window".
    file(STRINGS README.md heading REGEX "^# " LIMIT_COUNT 1)
    string(REGEX REPLACE "^# [0-9]+\\. " "" title "${heading}")

    eacp_add_app(${target} Main.cpp)
    target_link_libraries(${target} PRIVATE learneacp-chapter ${CHAPTER_LIBRARIES})
    set_target_properties(${target} PROPERTIES MACOSX_BUNDLE TRUE)

    if (NOT CHAPTER_CONSOLE)
        eacp_set_gui_subsystem(${target})
    endif ()

    set_default_target_setting(${target})

    set(library "${target}Chapter")
    add_library(${library} OBJECT Main.cpp)
    target_link_libraries(${library} PUBLIC learneacp-chapter ${CHAPTER_LIBRARIES})
    target_compile_definitions(${library} PRIVATE
            LEARNEACP_CHAPTER_REGISTRY
            LEARNEACP_CHAPTER_NUMBER=${number}
            LEARNEACP_CHAPTER_NAME="${target}"
            LEARNEACP_CHAPTER_TITLE="${title}")
    set_default_warnings_level(${library})

    set_property(GLOBAL APPEND PROPERTY LEARNEACP_CHAPTER_LIBRARIES ${library})
endfunction()
