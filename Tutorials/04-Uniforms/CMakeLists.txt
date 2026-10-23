eacp_add_app(Uniforms Main.cpp)
target_link_libraries(Uniforms PRIVATE eacp-gpu)
set_target_properties(Uniforms PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Uniforms)
set_default_target_setting(Uniforms)
