eacp_add_app(AntiAliasing Main.cpp)
target_link_libraries(AntiAliasing PRIVATE eacp-gpu)
set_target_properties(AntiAliasing PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(AntiAliasing)
set_default_target_setting(AntiAliasing)
