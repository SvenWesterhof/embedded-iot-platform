# Common shared code library
# This directory contains MCU-agnostic code shared between ESP32 and STM32

# For standalone CMake builds (STM32)
if(NOT ESP_PLATFORM)
    # Create interface library for header-only shared code
    add_library(common_shared INTERFACE)
    target_include_directories(common_shared INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # If there are source files in the future, create a static library
    file(GLOB COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
    if(COMMON_SOURCES)
        add_library(common_lib STATIC ${COMMON_SOURCES})
        target_include_directories(common_lib PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
    endif()
endif()
