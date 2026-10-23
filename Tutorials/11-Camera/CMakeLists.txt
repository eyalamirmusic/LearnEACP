eacp_add_app(Camera Main.cpp)
target_link_libraries(Camera PRIVATE eacp-gpu)
set_target_properties(Camera PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Camera)
set_default_target_setting(Camera)
