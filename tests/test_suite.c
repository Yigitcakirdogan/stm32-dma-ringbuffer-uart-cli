/**
 * @file    test_suite.c
 * @author  Yigit Cakirdogan
 * @brief   Host-side unit tests for lock-free SPSC ring buffer, Circular DMA RX
 *          wrap-around ingestion, and deterministic CLI command dispatch.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ring_buffer.h"
#include "uart_dma_driver.h"
#include "embedded_cli.h"

/* Simulated Hardware State for Unit Tests */
static size_t   mock_dma_ndtr     = UART_DMA_RX_BUF_SIZE;
static char     mock_tx_capture[2048];
static size_t   mock_tx_len       = 0U;
static bool     mock_gpio_a5      = false;
static bool     mock_reset_called = false;

static void mock_start_tx_dma(const uint8_t *data, uint16_t length)
{
    if ((mock_tx_len + length) < sizeof(mock_tx_capture)) {
        memcpy(&mock_tx_capture[mock_tx_len], data, length);
        mock_tx_len += length;
        mock_tx_capture[mock_tx_len] = '\0';
    }
}

static size_t mock_get_rx_dma_ndtr(void)
{
    return mock_dma_ndtr;
}

static void mock_system_reset(void)
{
    mock_reset_called = true;
}

static void mock_gpio_write(char port, uint8_t pin, bool state)
{
    if ((port == 'A') && (pin == 5U)) {
        mock_gpio_a5 = state;
    }
}

static bool mock_gpio_read(char port, uint8_t pin)
{
    if ((port == 'A') && (pin == 5U)) {
        return mock_gpio_a5;
    }
    return false;
}

static void test_ring_buffer_wrap_and_overflow(void)
{
    uint8_t storage[16];
    ring_buffer_t rb;

    /* Non-power-of-two capacity must be rejected */
    assert(!ring_buffer_init(&rb, storage, 15U));
    assert(ring_buffer_init(&rb, storage, 16U));
    assert(ring_buffer_capacity(&rb) == 15U);

    /* Fill 12 bytes, read 10 bytes to force index wrap-around on next write */
    const uint8_t pattern1[12] = "0123456789AB";
    assert(ring_buffer_write(&rb, pattern1, 12U) == 12U);

    uint8_t out[16] = {0};
    assert(ring_buffer_read(&rb, out, 10U) == 10U);
    assert(memcmp(out, "0123456789", 10U) == 0);
    assert(ring_buffer_available(&rb) == 2U);

    /* Write 13 bytes across the wrap-around boundary (fills to max capacity 15) */
    const uint8_t pattern2[13] = "CDEFGHIJKLMNO";
    assert(ring_buffer_write(&rb, pattern2, 13U) == 13U);
    assert(ring_buffer_available(&rb) == 15U);
    assert(ring_buffer_free_space(&rb) == 0U);

    /* Extra byte must fail and increment overflow counter */
    assert(!ring_buffer_put(&rb, 'Z'));
    assert(rb.overflow_count == 1U);
    assert(rb.high_watermark == 15U);

    /* Read all 15 bytes and verify exact FIFO ordering across wrap boundary */
    assert(ring_buffer_read(&rb, out, 15U) == 15U);
    assert(memcmp(out, "ABCDEFGHIJKLMNO", 15U) == 0);
    printf("[PASS] test_ring_buffer_wrap_and_overflow\n");
}

static void test_dma_circular_idle_ingestion(void)
{
    uart_dma_handle_t huart;
    uart_dma_hal_ops_t ops = {
        .start_tx_dma    = mock_start_tx_dma,
        .get_rx_dma_ndtr = mock_get_rx_dma_ndtr,
        .system_reset    = mock_system_reset,
        .gpio_write_pin  = mock_gpio_write,
        .gpio_read_pin   = mock_gpio_read
    };

    assert(uart_dma_init(&huart, &ops));

    /* Simulate DMA writing 10 bytes at end of 256-byte circular buffer (pos 250..4) */
    huart.last_dma_rx_pos = 250U;
    const char *burst = "HELLO_DMA!";
    memcpy(&huart.rx_dma_buffer[250], burst, 6U); /* 250..255 */
    memcpy(&huart.rx_dma_buffer[0], &burst[6], 4U); /* 0..3 */

    /* NDTR is remaining transfers: 256 - 4 = 252 */
    mock_dma_ndtr = 252U;
    uart_dma_rx_check_isr(&huart);

    char rx_out[16] = {0};
    size_t n = uart_dma_read(&huart, (uint8_t *)rx_out, sizeof(rx_out));
    assert(n == 10U);
    assert(memcmp(rx_out, "HELLO_DMA!", 10U) == 0);
    printf("[PASS] test_dma_circular_idle_ingestion\n");
}

static void test_cli_command_execution(void)
{
    uart_dma_handle_t huart;
    embedded_cli_t    cli;
    uart_dma_hal_ops_t ops = {
        .start_tx_dma    = mock_start_tx_dma,
        .get_rx_dma_ndtr = mock_get_rx_dma_ndtr,
        .system_reset    = mock_system_reset,
        .gpio_write_pin  = mock_gpio_write,
        .gpio_read_pin   = mock_gpio_read
    };

    mock_tx_len  = 0U;
    mock_gpio_a5 = false;

    assert(uart_dma_init(&huart, &ops));
    assert(embedded_cli_init(&cli, &huart, "mcu> "));

    /* Simulate receiving "gpio A 5 1\r" via DMA RX ring buffer */
    const char *cmd = "gpio A 5 1\r";
    (void)ring_buffer_write(&huart.rx_ring, (const uint8_t *)cmd, strlen(cmd));
    embedded_cli_poll(&cli);
    uart_dma_tx_complete_isr(&huart);

    assert(mock_gpio_a5 == true);
    assert(cli.commands_executed == 1U);
    assert(strstr(mock_tx_capture, "[GPIO] Port A Pin 5 -> SET (1)") != NULL);
    printf("[PASS] test_cli_command_execution\n");
}

int main(void)
{
    printf("=== Running STM32 UART DMA & Ring Buffer Test Suite ===\n");
    test_ring_buffer_wrap_and_overflow();
    test_dma_circular_idle_ingestion();
    test_cli_command_execution();
    printf("=== All tests passed successfully! ===\n");
    return 0;
}
