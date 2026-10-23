eacp_add_app(Lighting Main.cpp)
target_link_libraries(Lighting PRIVATE eacp-gpu)
set_target_properties(Lighting PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Lighting)
set_default_target_setting(Lighting)
