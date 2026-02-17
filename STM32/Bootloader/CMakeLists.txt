cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

project(bootloader C ASM)

# Bootloader executable
# Bootloader provides its own SystemInit, so system_stm32f7xx.c is not needed
add_executable(bootloader
    bootloader_main.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f767xx.s
)

# Include paths (minimal — only what the bootloader needs)
target_include_directories(bootloader PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F7xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)

target_compile_definitions(bootloader PRIVATE
    USE_HAL_DRIVER
    STM32F767xx
)

# STM32 HAL sources (only what the bootloader uses)
target_sources(bootloader PRIVATE
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
)

# Bootloader uses its own linker script
# Note: --specs=nano.specs is already set globally by the toolchain file
target_link_options(bootloader PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/bootloader_flash.ld
    -Wl,-Map=bootloader.map
)

# Generate .bin and .hex
add_custom_command(TARGET bootloader POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:bootloader> bootloader.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:bootloader> bootloader.hex
    COMMENT "Generating bootloader .bin and .hex"
)

add_custom_command(TARGET bootloader POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:bootloader>
    COMMENT "Bootloader size:"
)
