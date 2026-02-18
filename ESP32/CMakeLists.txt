# ESP32 Event-Driven Application
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF CMake utilities
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Add custom component directories
set(EXTRA_COMPONENT_DIRS
    Application
    Middleware
    Drivers_BSP
    HAL_Wrapper
    OS
    tests/app
    tests/mid
    tests/drv
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/esp_component
)

# Firmware version tracking
set(PROJECT_VER "1.1.0")
set(FIRMWARE_VERSION_MAJOR 1)
set(FIRMWARE_VERSION_MINOR 1)
set(FIRMWARE_VERSION_PATCH 0)

# Pass version information to application code
add_compile_definitions(
    FIRMWARE_VERSION="${PROJECT_VER}"
    FIRMWARE_VERSION_MAJOR=${FIRMWARE_VERSION_MAJOR}
    FIRMWARE_VERSION_MINOR=${FIRMWARE_VERSION_MINOR}
    FIRMWARE_VERSION_PATCH=${FIRMWARE_VERSION_PATCH}
)

# Project definition
project(iot_gateway)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
