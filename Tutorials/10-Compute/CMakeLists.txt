# A console program: it computes, prints and exits, so it opens no window and
# is not marked as a GUI app (on Windows that keeps its console output).
eacp_add_app(Compute Main.cpp)
target_link_libraries(Compute PRIVATE eacp-gpu eacp-cpu-compute)
set_target_properties(Compute PROPERTIES MACOSX_BUNDLE TRUE)

set_default_target_setting(Compute)
