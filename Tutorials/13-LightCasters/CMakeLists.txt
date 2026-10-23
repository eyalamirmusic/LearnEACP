eacp_add_app(LightCasters Main.cpp)
target_link_libraries(LightCasters PRIVATE eacp-gpu)
set_target_properties(LightCasters PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(LightCasters)
set_default_target_setting(LightCasters)
