/**
 * @file    ring_buffer.h
 * @author  Yigit Cakirdogan
 * @brief   Lock-free Single-Producer Single-Consumer (SPSC) ring buffer
 *          with zero-copy contiguous block access for DMA transfers.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Lock-free SPSC Ring Buffer Descriptor.
 * @note  Capacity must be a strict power of two (e.g., 64, 128, 256, 512, 1024)
 *        to allow fast bitwise masking instead of modulo division.
 */
typedef struct {
    uint8_t       *buffer;          /**< Pointer to caller-allocated static storage */
    size_t         capacity;        /**< Total buffer size in bytes (power of 2)    */
    size_t         mask;            /**< Bitmask (capacity - 1)                     */
    volatile size_t head;           /**< Write index (modified only by Producer)    */
    volatile size_t tail;           /**< Read index (modified only by Consumer)     */
    size_t         high_watermark;  /**< Peak occupancy recorded for diagnostics    */
    uint32_t       overflow_count;  /**< Number of dropped bytes due to full buffer */
} ring_buffer_t;

/**
 * @brief  Initialize a lock-free ring buffer over statically allocated memory.
 * @param  rb       Pointer to ring buffer instance.
 * @param  storage  Pointer to byte array of size @p capacity.
 * @param  capacity Size of @p storage in bytes (must be a power of 2 >= 2).
 * @return true if initialized successfully, false if parameters are invalid.
 */
bool ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t capacity);

/**
 * @brief  Reset head and tail indices (must only be called when producer/consumer are halted).
 */
void ring_buffer_reset(ring_buffer_t *rb);

/**
 * @brief  Write a single byte into the ring buffer (Producer / ISR context).
 * @return true if byte was written, false if buffer is full.
 */
bool ring_buffer_put(ring_buffer_t *rb, uint8_t data);

/**
 * @brief  Write a block of bytes into the ring buffer (DMA ISR / Producer context).
 * @return Number of bytes actually written.
 */
size_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t length);

/**
 * @brief  Read a single byte from the ring buffer (Consumer context).
 * @return true if a byte was retrieved, false if buffer is empty.
 */
bool ring_buffer_get(ring_buffer_t *rb, uint8_t *out_byte);

/**
 * @brief  Read up to @p max_length bytes from the ring buffer.
 * @return Number of bytes actually read.
 */
size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *dest, size_t max_length);

/**
 * @brief  Zero-copy DMA helper: Get pointer and length of the next contiguous
 *         readable block without copying bytes out.
 * @param  rb             Pointer to ring buffer instance.
 * @param  out_block_ptr  Output pointer set to the start of the contiguous region.
 * @return Number of contiguous bytes available before wrap-around or head.
 */
size_t ring_buffer_get_read_block(const ring_buffer_t *rb, const uint8_t **out_block_ptr);

/**
 * @brief  Advance the read tail after a zero-copy DMA transmission completes.
 * @param  rb             Pointer to ring buffer instance.
 * @param  bytes_consumed Number of bytes transmitted by DMA.
 */
void ring_buffer_advance_read(ring_buffer_t *rb, size_t bytes_consumed);

/**
 * @brief  Return the number of bytes currently stored in the ring buffer.
 */
size_t ring_buffer_available(const ring_buffer_t *rb);

/**
 * @brief  Return the remaining free space in bytes.
 */
size_t ring_buffer_free_space(const ring_buffer_t *rb);

/**
 * @brief  Return the maximum usable capacity (capacity - 1).
 */
size_t ring_buffer_capacity(const ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */
