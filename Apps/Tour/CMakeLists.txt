# The Tour: one app hosting every chapter, with its code beside its result.
#
# It links every chapter's registry build (the <Name>Chapter object libraries
# learneacp_add_chapter() lists in LEARNEACP_CHAPTER_LIBRARIES), so it has to
# be added after Tutorials/, and it embeds each chapter's Main.cpp as data.
get_property(chapters GLOBAL PROPERTY LEARNEACP_CHAPTER_LIBRARIES)
list(LENGTH chapters chapterCount)

eacp_add_app(Tour
        Chrome.cpp
        Main.cpp
        Panes.cpp
        Tour.cpp
        WidgetView.cpp)

target_link_libraries(Tour PRIVATE
        ${chapters}
        ECode::Editor
        ECode::Syntax)

learneacp_embed_chapter_sources(Tour)

# --list fails unless this many chapters registered.
target_compile_definitions(Tour PRIVATE TOUR_EXPECTED_CHAPTER_COUNT=${chapterCount})

set_target_properties(Tour PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Tour)
set_default_target_setting(Tour)
