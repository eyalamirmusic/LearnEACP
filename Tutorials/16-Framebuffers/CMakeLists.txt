eacp_add_app(Framebuffers Main.cpp)
target_link_libraries(Framebuffers PRIVATE eacp-gpu)
set_target_properties(Framebuffers PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(Framebuffers)
set_default_target_setting(Framebuffers)
