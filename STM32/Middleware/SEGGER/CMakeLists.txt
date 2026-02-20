# SEGGER library: RTT for logging, optionally SystemView for profiling
# Note: SystemView is currently disabled (see STM32/CMakeLists.txt USE_SEGGER_SYSTEMVIEW)
# Only compile RTT files to avoid trace macro redefinition warnings

set(SEGGER_SOURCES
    SEGGER/SEGGER_RTT.c
    SEGGER/SEGGER_RTT_printf.c
)

# Only include SystemView files if explicitly enabled
if(USE_SEGGER_SYSTEMVIEW)
    list(APPEND SEGGER_SOURCES
        SEGGER/SEGGER_SYSVIEW.c
        Config/SEGGER_SYSVIEW_Config_FreeRTOS.c
        Patch/SEGGER_SYSVIEW_FreeRTOS.c
    )
endif()

add_library(SEGGER_SystemView STATIC ${SEGGER_SOURCES})

target_include_directories(SEGGER_SystemView PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SEGGER
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Patch
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1
)

target_compile_definitions(SEGGER_SystemView PUBLIC
    RTT_USE_ASM=0
)