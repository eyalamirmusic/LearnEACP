# LEARNEACP_CHAPTER and the chapter registry, shared by every chapter.
add_library(learneacp-chapter INTERFACE)
target_include_directories(learneacp-chapter INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(learneacp-chapter INTERFACE eacp-gpu)
