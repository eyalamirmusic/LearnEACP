eacp_add_app(HelloWindow Main.cpp)
target_link_libraries(HelloWindow PRIVATE eacp-gpu)
set_target_properties(HelloWindow PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(HelloWindow)
set_default_target_setting(HelloWindow)
