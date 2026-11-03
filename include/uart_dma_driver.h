/**
 * @file    uart_dma_driver.h
 * @author  Yigit Cakirdogan
 * @brief   Zero-copy STM32 UART + Circular DMA driver with IDLE line detection.
 */

#ifndef UART_DMA_DRIVER_H
#define UART_DMA_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ring_buffer.h"

#define UART_DMA_RX_BUF_SIZE  256U   /**< Hardware circular DMA RX buffer size */
#define UART_RING_RX_SIZE     512U   /**< SPSC RX ring buffer capacity         */
#define UART_RING_TX_SIZE     512U   /**< SPSC TX ring buffer capacity         */

/**
 * @brief Low-level hardware operations table (enables host simulation & unit testing).
 */
typedef struct {
    void   (*start_tx_dma)(const uint8_t *data, uint16_t length);
    size_t (*get_rx_dma_ndtr)(void);
    void   (*system_reset)(void);
    void   (*gpio_write_pin)(char port, uint8_t pin, bool state);
    bool   (*gpio_read_pin)(char port, uint8_t pin);
} uart_dma_hal_ops_t;

/**
 * @brief UART DMA Driver Context.
 */
typedef struct {
    uint8_t            rx_dma_buffer[UART_DMA_RX_BUF_SIZE];
    uint8_t            rx_ring_storage[UART_RING_RX_SIZE];
    uint8_t            tx_ring_storage[UART_RING_TX_SIZE];
    ring_buffer_t      rx_ring;
    ring_buffer_t      tx_ring;
    size_t             last_dma_rx_pos;
    volatile bool      tx_dma_busy;
    volatile size_t    tx_dma_active_len;
    uint32_t           idle_irq_count;
    uint32_t           dma_ht_irq_count;
    uint32_t           dma_tc_irq_count;
    uart_dma_hal_ops_t hal_ops;
} uart_dma_handle_t;

/**
 * @brief Initialize UART DMA driver state and ring buffers.
 */
bool uart_dma_init(uart_dma_handle_t *huart, const uart_dma_hal_ops_t *ops);

/**
 * @brief Configure STM32F4 USART2 + DMA1 Stream5 (RX) / Stream6 (TX) registers.
 * @param apb1_clock_hz APB1 peripheral clock frequency (e.g., 42000000UL).
 * @param baudrate      Target baud rate (e.g., 115200UL or 921600UL).
 */
void uart_dma_stm32f4_hw_init(uart_dma_handle_t *huart, uint32_t apb1_clock_hz, uint32_t baudrate);

/**
 * @brief Core DMA RX ingestion handler called from DMA HT, DMA TC, and UART IDLE ISRs.
 */
void uart_dma_rx_check_isr(uart_dma_handle_t *huart);

/**
 * @brief DMA TX Transfer-Complete ISR handler; automatically kicks off next block if pending.
 */
void uart_dma_tx_complete_isr(uart_dma_handle_t *huart);

/**
 * @brief Queue bytes for non-blocking DMA transmission.
 */
size_t uart_dma_write(uart_dma_handle_t *huart, const uint8_t *data, size_t length);

/**
 * @brief Queue a null-terminated string for non-blocking DMA transmission.
 */
size_t uart_dma_print(uart_dma_handle_t *huart, const char *str);

/**
 * @brief Read received bytes from the RX ring buffer.
 */
size_t uart_dma_read(uart_dma_handle_t *huart, uint8_t *dest, size_t max_length);

#ifdef __cplusplus
}
#endif

#endif /* UART_DMA_DRIVER_H */
