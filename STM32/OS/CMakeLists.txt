# OS Layer CMakeLists.txt

file(GLOB_RECURSE OS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

target_sources(${PROJECT_NAME} PRIVATE ${OS_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
