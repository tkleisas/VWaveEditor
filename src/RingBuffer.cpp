#include "vwave/RingBuffer.h"

#include <algorithm>

namespace vwave {

FloatRingBuffer::FloatRingBuffer(size_t capacity)
    : m_capacity(std::max<size_t>(1, capacity)), m_data(m_capacity, 0.0f) {}

size_t FloatRingBuffer::availableRead() const {
    const size_t write = m_write.load(std::memory_order_acquire);
    const size_t read = m_read.load(std::memory_order_acquire);
    return write - read;
}

size_t FloatRingBuffer::availableWrite() const {
    return m_capacity - availableRead();
}

bool FloatRingBuffer::push(const float* samples, size_t count) {
    if (!samples || count == 0) return true;
    const size_t write = m_write.load(std::memory_order_relaxed);
    const size_t read = m_read.load(std::memory_order_acquire);
    if (count > m_capacity - (write - read)) return false;
    for (size_t i = 0; i < count; ++i)
        m_data[(write + i) % m_capacity] = samples[i];
    m_write.store(write + count, std::memory_order_release);
    return true;
}

size_t FloatRingBuffer::pop(float* destination, size_t count) {
    if (!destination || count == 0) return 0;
    const size_t read = m_read.load(std::memory_order_relaxed);
    const size_t write = m_write.load(std::memory_order_acquire);
    const size_t available = write - read;
    const size_t copied = std::min(count, available);
    for (size_t i = 0; i < copied; ++i)
        destination[i] = m_data[(read + i) % m_capacity];
    m_read.store(read + copied, std::memory_order_release);
    return copied;
}

size_t FloatRingBuffer::discard(size_t count) {
    const size_t read = m_read.load(std::memory_order_relaxed);
    const size_t write = m_write.load(std::memory_order_acquire);
    const size_t dropped = std::min(count, write - read);
    m_read.store(read + dropped, std::memory_order_release);
    return dropped;
}

void FloatRingBuffer::clear() {
    m_read.store(0, std::memory_order_relaxed);
    m_write.store(0, std::memory_order_release);
}

} // namespace vwave
