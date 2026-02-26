cmake_minimum_required(VERSION 3.22)
project(host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Unity test framework
# ---------------------------------------------------------------------------
add_library(unity STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/Unity/src/unity.c
)
target_include_directories(unity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/Unity/src
)

# ---------------------------------------------------------------------------
# CMock framework (depends on Unity)
# ---------------------------------------------------------------------------
add_library(cmock STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/CMock/src/cmock.c
)
target_include_directories(cmock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/CMock/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/Unity/src
)
target_link_libraries(cmock PUBLIC unity)

# ---------------------------------------------------------------------------
# Common library compiled for host (x86-64, no FreeRTOS, no HAL hardware)
# os_wrapper.c is excluded here — it wraps FreeRTOS and will be mocked.
# ---------------------------------------------------------------------------
add_library(common_native STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/crc16.c
)
target_include_directories(common_native PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
target_compile_definitions(common_native PRIVATE HOST_TEST=1)

# ---------------------------------------------------------------------------
# CMock mock generation from HAL interface headers
# Requires Ruby to be installed (used by CMock's code generator).
# Generated files are written to ${CMAKE_BINARY_DIR}/mocks — not committed.
# ---------------------------------------------------------------------------
find_program(RUBY_EXECUTABLE ruby)

set(MOCK_HEADERS
    # Note: hal_uart_interface.h and hal_gpio_interface.h use the Strategy pattern
    # (function pointers inside a struct) so CMock cannot auto-generate mocks from them.
    # Tests that need HAL stubs should define a concrete hal_uart_interface_t /
    # hal_gpio_interface_t struct with custom function pointers and assign it to
    # hal_uart / hal_gpio in setUp().
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include/os_wrapper.h
)
set(MOCK_OUTPUT_DIR ${CMAKE_BINARY_DIR}/mocks)
file(MAKE_DIRECTORY ${MOCK_OUTPUT_DIR})

set(MOCK_SOURCES "")
if(RUBY_EXECUTABLE)
    foreach(HEADER ${MOCK_HEADERS})
        get_filename_component(HEADER_NAME ${HEADER} NAME_WE)
        set(MOCK_SRC ${MOCK_OUTPUT_DIR}/mock_${HEADER_NAME}.c)
        set(MOCK_HDR ${MOCK_OUTPUT_DIR}/mock_${HEADER_NAME}.h)
        add_custom_command(
            OUTPUT ${MOCK_SRC} ${MOCK_HDR}
            COMMAND ${RUBY_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/vendor/CMock/lib/cmock.rb
                --mock_path=${MOCK_OUTPUT_DIR}
                --mock_prefix=mock_
                "--plugins=ignore;"
                ${HEADER}
            DEPENDS ${HEADER}
            COMMENT "Generating mock for ${HEADER_NAME}"
        )
        list(APPEND MOCK_SOURCES ${MOCK_SRC})
    endforeach()
else()
    message(WARNING "Ruby not found — CMock mock generation disabled. Install Ruby to enable mocks.")
endif()

if(MOCK_SOURCES)
    add_library(mocks STATIC ${MOCK_SOURCES})
    target_include_directories(mocks PUBLIC
        ${MOCK_OUTPUT_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/CMock/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/Unity/src
    )
    target_link_libraries(mocks PUBLIC cmock)
else()
    # Stub target so subdirectories can always link against 'mocks'
    add_library(mocks INTERFACE)
    target_link_libraries(mocks INTERFACE cmock)
endif()

# ---------------------------------------------------------------------------
# Helper macro: register a Unity test executable with CTest
# Usage: add_unity_test(test_name test_source.c)
# ---------------------------------------------------------------------------
macro(add_unity_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        unity
        mocks
        common_native
    )
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
        ${MOCK_OUTPUT_DIR}
    )
    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
    )
endmacro()

# ---------------------------------------------------------------------------
# Enable CTest and add subdirectory test suites
# ---------------------------------------------------------------------------
enable_testing()

add_subdirectory(common)
# add_subdirectory(esp32)   # Uncomment when ESP32-specific tests are added
add_subdirectory(stm32)
