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
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/esp_component
)

# Project definition
project(iot_gateway)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
