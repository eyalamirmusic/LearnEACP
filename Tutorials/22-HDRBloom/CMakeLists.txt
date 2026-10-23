eacp_add_app(HDRBloom Main.cpp)
target_link_libraries(HDRBloom PRIVATE eacp-gpu)
set_target_properties(HDRBloom PROPERTIES MACOSX_BUNDLE TRUE)

eacp_set_gui_subsystem(HDRBloom)
set_default_target_setting(HDRBloom)
