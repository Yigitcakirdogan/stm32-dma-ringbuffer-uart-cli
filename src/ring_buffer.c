/**
 * @file    ring_buffer.c
 * @author  Yigit Cakirdogan
 * @brief   Implementation of lock-free SPSC ring buffer with zero-copy DMA support.
 */

#include "ring_buffer.h"
#include <string.h>

#if defined(__ARM_ARCH)
    #define RB_MEMORY_BARRIER() __asm volatile("dmb" ::: "memory")
#elif defined(__GNUC__) || defined(__clang__)
    #define RB_MEMORY_BARRIER() __sync_synchronize()
#else
    #define RB_MEMORY_BARRIER() do {} while (0)
#endif

static bool is_power_of_two(size_t n)
{
    return (n >= 2U) && ((n & (n - 1U)) == 0U);
}

static void update_watermark(ring_buffer_t *rb, size_t current_level)
{
    if (current_level > rb->high_watermark) {
        rb->high_watermark = current_level;
    }
}

bool ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t capacity)
{
    if ((rb == NULL) || (storage == NULL) || !is_power_of_two(capacity)) {
        return false;
    }

    rb->buffer         = storage;
    rb->capacity       = capacity;
    rb->mask           = capacity - 1U;
    rb->head           = 0U;
    rb->tail           = 0U;
    rb->high_watermark = 0U;
    rb->overflow_count = 0U;
    return true;
}

void ring_buffer_reset(ring_buffer_t *rb)
{
    if (rb != NULL) {
        rb->head = 0U;
        rb->tail = 0U;
    }
}

size_t ring_buffer_available(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0U;
    }
    return (rb->head - rb->tail) & rb->mask;
}

size_t ring_buffer_capacity(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0U;
    }
    return rb->mask;
}

size_t ring_buffer_free_space(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0U;
    }
    return rb->mask - ring_buffer_available(rb);
}

bool ring_buffer_put(ring_buffer_t *rb, uint8_t data)
{
    if (rb == NULL) {
        return false;
    }

    size_t head      = rb->head;
    size_t next_head = (head + 1U) & rb->mask;

    if (next_head == rb->tail) {
        rb->overflow_count++;
        return false;
    }

    rb->buffer[head] = data;
    RB_MEMORY_BARRIER();
    rb->head = next_head;

    update_watermark(rb, (next_head - rb->tail) & rb->mask);
    return true;
}

size_t ring_buffer_write(ring_buffer_t *rb, const uint8_t *data, size_t length)
{
    if ((rb == NULL) || (data == NULL) || (length == 0U)) {
        return 0U;
    }

    size_t free_bytes = ring_buffer_free_space(rb);
    size_t to_write   = (length > free_bytes) ? free_bytes : length;

    if (length > free_bytes) {
        rb->overflow_count += (uint32_t)(length - free_bytes);
    }

    if (to_write == 0U) {
        return 0U;
    }

    size_t head       = rb->head;
    size_t first_part = rb->capacity - head;

    if (first_part > to_write) {
        first_part = to_write;
    }

    memcpy(&rb->buffer[head], data, first_part);

    size_t second_part = to_write - first_part;
    if (second_part > 0U) {
        memcpy(&rb->buffer[0], &data[first_part], second_part);
    }

    RB_MEMORY_BARRIER();
    rb->head = (head + to_write) & rb->mask;

    update_watermark(rb, ring_buffer_available(rb));
    return to_write;
}

bool ring_buffer_get(ring_buffer_t *rb, uint8_t *out_byte)
{
    if ((rb == NULL) || (out_byte == NULL)) {
        return false;
    }

    size_t tail = rb->tail;
    if (tail == rb->head) {
        return false;
    }

    *out_byte = rb->buffer[tail];
    RB_MEMORY_BARRIER();
    rb->tail = (tail + 1U) & rb->mask;
    return true;
}

size_t ring_buffer_read(ring_buffer_t *rb, uint8_t *dest, size_t max_length)
{
    if ((rb == NULL) || (dest == NULL) || (max_length == 0U)) {
        return 0U;
    }

    size_t avail   = ring_buffer_available(rb);
    size_t to_read = (max_length > avail) ? avail : max_length;

    if (to_read == 0U) {
        return 0U;
    }

    size_t tail       = rb->tail;
    size_t first_part = rb->capacity - tail;

    if (first_part > to_read) {
        first_part = to_read;
    }

    memcpy(dest, &rb->buffer[tail], first_part);

    size_t second_part = to_read - first_part;
    if (second_part > 0U) {
        memcpy(&dest[first_part], &rb->buffer[0], second_part);
    }

    RB_MEMORY_BARRIER();
    rb->tail = (tail + to_read) & rb->mask;
    return to_read;
}

size_t ring_buffer_get_read_block(const ring_buffer_t *rb, const uint8_t **out_block_ptr)
{
    if ((rb == NULL) || (out_block_ptr == NULL)) {
        return 0U;
    }

    size_t head = rb->head;
    size_t tail = rb->tail;

    if (head == tail) {
        *out_block_ptr = NULL;
        return 0U;
    }

    *out_block_ptr = &rb->buffer[tail];

    if (head > tail) {
        return head - tail;
    }
    return rb->capacity - tail;
}

void ring_buffer_advance_read(ring_buffer_t *rb, size_t bytes_consumed)
{
    if (rb == NULL) {
        return;
    }

    size_t avail = ring_buffer_available(rb);
    if (bytes_consumed > avail) {
        bytes_consumed = avail;
    }

    RB_MEMORY_BARRIER();
    rb->tail = (rb->tail + bytes_consumed) & rb->mask;
}
