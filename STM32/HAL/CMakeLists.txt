# HAL (Hardware Abstraction Layer) CMakeLists.txt

file(GLOB_RECURSE HAL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

target_sources(${PROJECT_NAME} PRIVATE ${HAL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
