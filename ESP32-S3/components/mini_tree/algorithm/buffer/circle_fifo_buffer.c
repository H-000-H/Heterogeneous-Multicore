/*
 * 无锁 SPSC 环形缓冲区 — 并发合约参见 m_buffer.h。
 * 禁止在没有外部互斥锁的情况下向同一个 struct fifo_spsc 添加第二个生产者或消费者。
 */
#include "m_buffer.h"

#ifndef NDEBUG
#include <assert.h>
#endif

void fifo_init(struct fifo_spsc* handle, Fifo_Data_type* buf, uint16_t size)
{
#ifndef NDEBUG
    assert(FIFO_IS_POWER_OF_TWO(size));
#endif
    handle->buf = buf;
    handle->size = size;
    atomic_init(&handle->r_ptr, 0);
    atomic_init(&handle->w_ptr, 0);
}

bool fifo_write_data(struct fifo_spsc* handle, Fifo_Data_type data)
{
    uint16_t mask = handle->size - 1;
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_relaxed);

    if (((w + 1) & mask) == r)
    {
        return false;
    }

    handle->buf[w] = data;
    atomic_store_explicit(&handle->w_ptr, (uint16_t)((w + 1) & mask), memory_order_release);
    return true;
}

uint16_t fifo_write_block(struct fifo_spsc* handle, const Fifo_Data_type* p_data, uint16_t len)
{
    if (len == 0) return 0;

    uint16_t mask = handle->size - 1;
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_relaxed);
    uint16_t used = (uint16_t)((w - r) & mask);
    uint16_t free_len = mask - used;

    if (len > free_len)
    {
        len = free_len;
    }
    if (len == 0)
    {
        return 0;
    }

    uint16_t space_to_end = handle->size - w;
    if (space_to_end >= len)
    {
        __builtin_memcpy(&handle->buf[w], p_data, len * sizeof(Fifo_Data_type));
    }
    else
    {
        __builtin_memcpy(&handle->buf[w], p_data, space_to_end * sizeof(Fifo_Data_type));
        __builtin_memcpy(&handle->buf[0], p_data + space_to_end, (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    atomic_store_explicit(&handle->w_ptr, (uint16_t)((w + len) & mask), memory_order_release);
    return len;
}

bool fifo_read_data(struct fifo_spsc* handle, Fifo_Data_type* p_data)
{
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_relaxed);

    if (r == w)
    {
        return false;
    }

    *p_data = handle->buf[r];
    uint16_t mask = handle->size - 1;
    atomic_store_explicit(&handle->r_ptr, (uint16_t)((r + 1) & mask), memory_order_release);
    return true;
}

uint16_t fifo_read_block(struct fifo_spsc* handle, Fifo_Data_type* p_data, uint16_t len)
{
    if (len == 0) return 0;

    uint16_t mask = handle->size - 1;
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_relaxed);
    uint16_t count = (uint16_t)((w - r) & mask);

    if (len > count)
    {
        len = count;
    }
    if (len == 0) return 0;

    uint16_t space_to_end = handle->size - r;
    if (space_to_end >= len)
    {
        __builtin_memcpy(p_data, &handle->buf[r], len * sizeof(Fifo_Data_type));
    }
    else
    {
        __builtin_memcpy(p_data, &handle->buf[r], space_to_end * sizeof(Fifo_Data_type));
        __builtin_memcpy(p_data + space_to_end, &handle->buf[0], (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    atomic_store_explicit(&handle->r_ptr, (uint16_t)((r + len) & mask), memory_order_release);
    return len;
}

uint16_t fifo_get_count(struct fifo_spsc* handle)
{
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    return (uint16_t)((w - r) & (handle->size - 1));
}

bool fifo_isempty(struct fifo_spsc* handle)
{
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_relaxed);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    return r == w;
}

bool fifo_isfull(struct fifo_spsc* handle)
{
    uint16_t mask = handle->size - 1;
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_relaxed);
    return ((w + 1) & mask) == r;
}
