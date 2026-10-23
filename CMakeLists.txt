cmake_minimum_required(VERSION 3.31)

project(LearnEACP LANGUAGES C CXX)

# EACP's Apple backends are Objective-C++, so the language has to be enabled
# before EACP is added, exactly as EACP does in its own top-level file.
if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json for clangd and other tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(CPM)

# EACP itself. Its examples and tests stay off because it is not the top-level
# project here. Adding it also defines the helpers each chapter uses:
# eacp_add_app, eacp_set_gui_subsystem and set_default_target_setting.
#
# NO_CACHE keeps EACP out of CPM_SOURCE_CACHE: the cache is keyed on these
# arguments, so with GIT_TAG main it would pin whatever main was on the first
# download forever. EACP is always cloned fresh into build/_deps/eacp-src; its
# own dependencies still come from the cache.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        NO_CACHE YES)

add_subdirectory(Tutorials)
