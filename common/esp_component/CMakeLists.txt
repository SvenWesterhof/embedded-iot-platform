# ESP-IDF Component for common shared code
idf_component_register(
    SRCS
        "../src/crc16.c"
        "../src/os_wrapper.c"
    INCLUDE_DIRS
        "../include"
    REQUIRES
        freertos
)
