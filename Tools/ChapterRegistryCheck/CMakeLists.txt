# A temporary console program: links every chapter's registry build into one
# binary, which proves they link together without duplicate symbols, prints
# the registered chapters in order and fails unless all of them are there.
get_property(chapters GLOBAL PROPERTY LEARNEACP_CHAPTER_LIBRARIES)
list(LENGTH chapters chapterCount)

eacp_add_app(ChapterRegistryCheck Main.cpp)
target_link_libraries(ChapterRegistryCheck PRIVATE ${chapters})
target_compile_definitions(ChapterRegistryCheck PRIVATE
        EXPECTED_CHAPTER_COUNT=${chapterCount})
set_target_properties(ChapterRegistryCheck PROPERTIES
        MACOSX_BUNDLE TRUE
        FOLDER Tools)

set_default_target_setting(ChapterRegistryCheck)
