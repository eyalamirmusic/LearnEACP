eacp_add_app(LightingMaps Main.cpp)
target_link_libraries(LightingMaps PRIVATE eacp-gpu)
set_target_properties(LightingMaps PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(LightingMaps)
set_default_target_setting(LightingMaps)
