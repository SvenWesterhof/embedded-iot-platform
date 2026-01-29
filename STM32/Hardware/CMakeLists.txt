file(GLOB HARDWARE_SOURCES "*.c")
target_sources(${PROJECT_NAME} PRIVATE ${HARDWARE_SOURCES})

# Optional: include your headers
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)