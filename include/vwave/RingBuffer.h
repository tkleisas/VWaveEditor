#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace vwave {

class FloatRingBuffer {
public:
    explicit FloatRingBuffer(size_t capacity = 1u << 20);

    size_t capacity() const { return m_capacity; }
    size_t availableRead() const;
    size_t availableWrite() const;

    bool push(const float* samples, size_t count);
    size_t pop(float* destination, size_t count);
    size_t discard(size_t count);
    void clear();

private:
    size_t m_capacity = 0;
    std::vector<float> m_data;
    std::atomic<size_t> m_read{0};
    std::atomic<size_t> m_write{0};
};

} // namespace vwave
