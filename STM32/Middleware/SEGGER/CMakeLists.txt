add_library(SEGGER_SystemView STATIC
    SEGGER/SEGGER_RTT.c
    SEGGER/SEGGER_RTT_printf.c
    SEGGER/SEGGER_SYSVIEW.c
    Config/SEGGER_SYSVIEW_Config_FreeRTOS.c
    Patch/SEGGER_SYSVIEW_FreeRTOS.c
)

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