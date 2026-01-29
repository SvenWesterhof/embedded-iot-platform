# Middleware Layer CMakeLists.txt

file(GLOB_RECURSE MIDDLEWARE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Control/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Services/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Features/*.c"
)

target_sources(${PROJECT_NAME} PRIVATE ${MIDDLEWARE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Control"
    "${CMAKE_CURRENT_SOURCE_DIR}/Services"
    "${CMAKE_CURRENT_SOURCE_DIR}/Features"
)
