# STM32-specific host-native unit tests
#
# Source files under test are compiled into stm32_utils_native (and similar
# libraries as more modules are added). All STM32 platform headers are added
# here so the code under test compiles without the ARM GCC toolchain.
#
# Mock setup: mocks and add_unity_test() are inherited from the parent
# CMakeLists. Add new mockable headers to MOCK_HEADERS in the parent.

# ---------------------------------------------------------------------------
# STM32 Utils — sensor_ring_buffer and friends
# ---------------------------------------------------------------------------
add_library(stm32_utils_native STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32/Utils/sensor_ring_buffer.c
)
target_include_directories(stm32_utils_native PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32/Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
    ${MOCK_OUTPUT_DIR}
)
target_link_libraries(stm32_utils_native PUBLIC mocks common_native)
target_compile_definitions(stm32_utils_native PRIVATE HOST_TEST=1)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_unity_test(test_sensor_ring_buffer test_sensor_ring_buffer.c)
target_link_libraries(test_sensor_ring_buffer PRIVATE stm32_utils_native)
target_include_directories(test_sensor_ring_buffer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32/Utils
)
