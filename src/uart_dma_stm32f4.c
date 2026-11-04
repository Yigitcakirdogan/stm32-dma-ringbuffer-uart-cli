/**
 * @file    uart_dma_stm32f4.c
 * @author  Yigit Cakirdogan
 * @brief   STM32F4 register-level USART2 + DMA1 driver and ISR state machine.
 */

#include "uart_dma_driver.h"
#include <string.h>

/* ==========================================================================
 * STM32F407 Memory-Mapped Register Definitions (when compiled for bare-metal)
 * ========================================================================== */
#if defined(STM32F407xx)
#include "stm32f4xx.h"
#else
/* Portable register stubs for host-side compilation & verification */
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} usart_regs_t;

typedef struct {
    volatile uint32_t CR;
    volatile uint32_t NDTR;
    volatile uint32_t PAR;
    volatile uint32_t M0AR;
    volatile uint32_t M1AR;
    volatile uint32_t FCR;
} dma_stream_regs_t;

static usart_regs_t      sim_usart2;
static dma_stream_regs_t sim_dma1_stream5;
static dma_stream_regs_t sim_dma1_stream6;

#define USART2_BASE_PTR       (&sim_usart2)
#define DMA1_STREAM5_BASE_PTR (&sim_dma1_stream5)
#define DMA1_STREAM6_BASE_PTR (&sim_dma1_stream6)

#define USART_CR1_UE          (1UL << 13)
#define USART_CR1_IDLEIE      (1UL << 4)
#define USART_CR1_TE          (1UL << 3)
#define USART_CR1_RE          (1UL << 2)
#define USART_CR3_DMAT        (1UL << 7)
#define USART_CR3_DMAR        (1UL << 6)
#define DMA_SxCR_EN           (1UL << 0)
#define DMA_SxCR_TCIE         (1UL << 4)
#define DMA_SxCR_HTIE         (1UL << 3)
#define DMA_SxCR_CIRC         (1UL << 8)
#define DMA_SxCR_MINC         (1UL << 10)
#endif

static void kick_tx_dma_if_idle(uart_dma_handle_t *huart)
{
    if (huart->tx_dma_busy) {
        return;
    }

    const uint8_t *block_ptr = NULL;
    size_t block_len = ring_buffer_get_read_block(&huart->tx_ring, &block_ptr);

    if ((block_len > 0U) && (block_ptr != NULL)) {
        huart->tx_dma_busy       = true;
        huart->tx_dma_active_len = block_len;
        if (huart->hal_ops.start_tx_dma != NULL) {
            huart->hal_ops.start_tx_dma(block_ptr, (uint16_t)block_len);
        }
    }
}

bool uart_dma_init(uart_dma_handle_t *huart, const uart_dma_hal_ops_t *ops)
{
    if (huart == NULL) {
        return false;
    }

    memset(huart, 0, sizeof(*huart));

    if (ops != NULL) {
        huart->hal_ops = *ops;
    }

    bool rx_ok = ring_buffer_init(&huart->rx_ring, huart->rx_ring_storage, UART_RING_RX_SIZE);
    bool tx_ok = ring_buffer_init(&huart->tx_ring, huart->tx_ring_storage, UART_RING_TX_SIZE);

    return rx_ok && tx_ok;
}

void uart_dma_stm32f4_hw_init(uart_dma_handle_t *huart, uint32_t apb1_clock_hz, uint32_t baudrate)
{
    if ((huart == NULL) || (baudrate == 0U)) {
        return;
    }

    /* Configure DMA1 Stream 5 (Channel 4) for Circular USART2_RX */
    DMA1_STREAM5_BASE_PTR->CR   = 0U;
    DMA1_STREAM5_BASE_PTR->PAR  = (uint32_t)(uintptr_t)&USART2_BASE_PTR->DR;
    DMA1_STREAM5_BASE_PTR->M0AR = (uint32_t)(uintptr_t)huart->rx_dma_buffer;
    DMA1_STREAM5_BASE_PTR->NDTR = UART_DMA_RX_BUF_SIZE;
    DMA1_STREAM5_BASE_PTR->CR   = (4UL << 25) /* Channel 4 */
                                | DMA_SxCR_MINC
                                | DMA_SxCR_CIRC
                                | DMA_SxCR_HTIE
                                | DMA_SxCR_TCIE
                                | DMA_SxCR_EN;

    /* Configure DMA1 Stream 6 (Channel 4) for USART2_TX */
    DMA1_STREAM6_BASE_PTR->CR   = 0U;
    DMA1_STREAM6_BASE_PTR->PAR  = (uint32_t)(uintptr_t)&USART2_BASE_PTR->DR;

    /* Configure USART2 Baudrate & Enable DMA RX/TX + IDLE Line Interrupt */
    USART2_BASE_PTR->BRR = (apb1_clock_hz + (baudrate / 2U)) / baudrate;
    USART2_BASE_PTR->CR3 = USART_CR3_DMAR | USART_CR3_DMAT;
    USART2_BASE_PTR->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_IDLEIE;
}

void uart_dma_rx_check_isr(uart_dma_handle_t *huart)
{
    if (huart == NULL) {
        return;
    }

    size_t ndtr = 0U;
    if (huart->hal_ops.get_rx_dma_ndtr != NULL) {
        ndtr = huart->hal_ops.get_rx_dma_ndtr();
    } else {
        ndtr = (size_t)DMA1_STREAM5_BASE_PTR->NDTR;
    }

    if (ndtr > UART_DMA_RX_BUF_SIZE) {
        return;
    }

    size_t current_pos = UART_DMA_RX_BUF_SIZE - ndtr;
    if (current_pos == UART_DMA_RX_BUF_SIZE) {
        current_pos = 0U;
    }

    if (current_pos != huart->last_dma_rx_pos) {
        if (current_pos > huart->last_dma_rx_pos) {
            /* Linear region within circular DMA buffer */
            size_t delta = current_pos - huart->last_dma_rx_pos;
            (void)ring_buffer_write(&huart->rx_ring,
                                    &huart->rx_dma_buffer[huart->last_dma_rx_pos],
                                    delta);
        } else {
            /* Wrapped around end of circular DMA buffer */
            size_t tail_len = UART_DMA_RX_BUF_SIZE - huart->last_dma_rx_pos;
            (void)ring_buffer_write(&huart->rx_ring,
                                    &huart->rx_dma_buffer[huart->last_dma_rx_pos],
                                    tail_len);
            if (current_pos > 0U) {
                (void)ring_buffer_write(&huart->rx_ring,
                                        &huart->rx_dma_buffer[0],
                                        current_pos);
            }
        }
        huart->last_dma_rx_pos = current_pos;
    }
}

void uart_dma_tx_complete_isr(uart_dma_handle_t *huart)
{
    if (huart == NULL) {
        return;
    }

    if (huart->tx_dma_busy) {
        ring_buffer_advance_read(&huart->tx_ring, huart->tx_dma_active_len);
        huart->tx_dma_active_len = 0U;
        huart->tx_dma_busy       = false;
    }

    kick_tx_dma_if_idle(huart);
}

size_t uart_dma_write(uart_dma_handle_t *huart, const uint8_t *data, size_t length)
{
    if ((huart == NULL) || (data == NULL) || (length == 0U)) {
        return 0U;
    }

    size_t written = ring_buffer_write(&huart->tx_ring, data, length);
    kick_tx_dma_if_idle(huart);
    return written;
}

size_t uart_dma_print(uart_dma_handle_t *huart, const char *str)
{
    if ((huart == NULL) || (str == NULL)) {
        return 0U;
    }
    return uart_dma_write(huart, (const uint8_t *)str, strlen(str));
}

size_t uart_dma_read(uart_dma_handle_t *huart, uint8_t *dest, size_t max_length)
{
    if (huart == NULL) {
        return 0U;
    }
    return ring_buffer_read(&huart->rx_ring, dest, max_length);
}
