eacp_add_app(Textures Main.cpp)
target_link_libraries(Textures PRIVATE eacp-gpu)
set_target_properties(Textures PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Textures)
set_default_target_setting(Textures)
