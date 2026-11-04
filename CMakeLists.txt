cmake_minimum_required(VERSION 3.16)
project(stm32_dma_ringbuffer_uart_cli VERSION 1.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wpedantic -Wshadow -ffunction-sections -fdata-sections)
endif()

add_library(uart_dma_cli STATIC
    src/ring_buffer.c
    src/uart_dma_stm32f4.c
    src/embedded_cli.c
)

target_include_directories(uart_dma_cli PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()

add_executable(test_suite tests/test_suite.c)
target_link_libraries(test_suite PRIVATE uart_dma_cli)
target_compile_options(test_suite PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable)
add_test(NAME FirmwareUnitTestSuite COMMAND test_suite)
