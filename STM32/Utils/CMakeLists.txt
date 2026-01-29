# Utils Layer CMakeLists.txt
# Generic utility data structures and helpers

file(GLOB_RECURSE UTILS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

target_sources(${PROJECT_NAME} PRIVATE ${UTILS_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
