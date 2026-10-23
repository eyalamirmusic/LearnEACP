eacp_add_app(Cubemaps Main.cpp)
target_link_libraries(Cubemaps PRIVATE eacp-gpu)
set_target_properties(Cubemaps PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Cubemaps)
set_default_target_setting(Cubemaps)
