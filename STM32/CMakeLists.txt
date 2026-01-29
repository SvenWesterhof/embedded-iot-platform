cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME sensor_node)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# CRITICAL: Configure FreeRTOS with SEGGER SystemView trace macros (survives STM32CubeMX regeneration)
# Without this, FreeRTOS kernel sources (tasks.c, queue.c, etc.) compile without trace macros
# This MUST be after add_subdirectory(cmake/stm32cubemx) so the FreeRTOS target exists
target_compile_definitions(FreeRTOS PRIVATE USE_SEGGER_SYSTEMVIEW=1)
target_include_directories(FreeRTOS PRIVATE
    ${CMAKE_SOURCE_DIR}/Middleware/SEGGER/Patch
    ${CMAKE_SOURCE_DIR}/Middleware/SEGGER/SEGGER
)

# Common shared code (MCU-agnostic)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)

# New layered architecture subdirectories
add_subdirectory(Application)
add_subdirectory(Middleware)
add_subdirectory(HAL)
add_subdirectory(Drivers_BSP)
add_subdirectory(OS)
add_subdirectory(Utils)
add_subdirectory(Middleware/SEGGER)

# Add common include directory
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)
target_link_options(${PROJECT_NAME} PRIVATE "-u" "_printf_float")


# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # SEGGER SystemView configuration (survives STM32CubeMX regeneration)
    # Temporarily disabled to test if SystemView overhead causes crashes
    USE_SEGGER_SYSTEMVIEW=1
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    SEGGER_SystemView
    common_lib
)

# Generate binary and hex files for flashing
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMENT "Generating .bin and .hex files for flashing"
)

# Print file sizes and locations
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "File sizes:"
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Flash files generated:"
    COMMAND ${CMAKE_COMMAND} -E echo "  Binary: ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
    COMMAND ${CMAKE_COMMAND} -E echo "  Hex:    ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex"
    COMMAND ${CMAKE_COMMAND} -E echo "  ELF:    $<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
)
