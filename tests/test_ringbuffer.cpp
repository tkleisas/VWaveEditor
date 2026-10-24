#include "test_framework.h"

#include "vwave/RingBuffer.h"

#include <vector>

using namespace vwave;

VWTEST("ring buffer reports free and used space") {
    FloatRingBuffer ring(8);
    CHECK_EQ(ring.capacity(), size_t{8});
    CHECK_EQ(ring.availableRead(), size_t{0});
    CHECK_EQ(ring.availableWrite(), size_t{8});
    const float values[] = {1.0f, 2.0f, 3.0f};
    REQUIRE(ring.push(values, 3));
    CHECK_EQ(ring.availableRead(), size_t{3});
    CHECK_EQ(ring.availableWrite(), size_t{5});
}

VWTEST("ring buffer pushes and pops in order") {
    FloatRingBuffer ring(8);
    const float values[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    REQUIRE(ring.push(values, 5));
    std::vector<float> out(3);
    CHECK_EQ(ring.pop(out.data(), 3), size_t{3});
    CHECK_NEAR(out[0], 1.0, 1e-9);
    CHECK_NEAR(out[1], 2.0, 1e-9);
    CHECK_NEAR(out[2], 3.0, 1e-9);
    std::vector<float> rest(5);
    CHECK_EQ(ring.pop(rest.data(), 5), size_t{2});
    CHECK_NEAR(rest[0], 4.0, 1e-9);
    CHECK_NEAR(rest[1], 5.0, 1e-9);
    CHECK_EQ(ring.availableRead(), size_t{0});
}

VWTEST("ring buffer rejects pushes that do not fit") {
    FloatRingBuffer ring(4);
    const float values[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    REQUIRE(ring.push(values, 4));
    CHECK(!ring.push(values, 1));
    CHECK_EQ(ring.availableRead(), size_t{4});
    std::vector<float> out(4);
    CHECK_EQ(ring.pop(out.data(), 4), size_t{4});
    REQUIRE(ring.push(values, 4));
    CHECK_EQ(ring.availableRead(), size_t{4});
}

VWTEST("ring buffer wraps around") {
    FloatRingBuffer ring(4);
    const float first[] = {1.0f, 2.0f, 3.0f};
    REQUIRE(ring.push(first, 3));
    float out[2];
    CHECK_EQ(ring.pop(out, 2), size_t{2});
    const float second[] = {4.0f, 5.0f, 6.0f};
    REQUIRE(ring.push(second, 3));
    float result[4];
    CHECK_EQ(ring.pop(result, 4), size_t{4});
    CHECK_NEAR(result[0], 3.0, 1e-9);
    CHECK_NEAR(result[1], 4.0, 1e-9);
    CHECK_NEAR(result[2], 5.0, 1e-9);
    CHECK_NEAR(result[3], 6.0, 1e-9);
}

VWTEST("ring buffer discard and clear") {
    FloatRingBuffer ring(8);
    const float values[] = {1.0f, 2.0f, 3.0f, 4.0f};
    REQUIRE(ring.push(values, 4));
    CHECK_EQ(ring.discard(2), size_t{2});
    CHECK_EQ(ring.availableRead(), size_t{2});
    float out[4];
    CHECK_EQ(ring.pop(out, 4), size_t{2});
    CHECK_NEAR(out[0], 3.0, 1e-9);
    REQUIRE(ring.push(values, 4));
    ring.clear();
    CHECK_EQ(ring.availableRead(), size_t{0});
    CHECK_EQ(ring.availableWrite(), size_t{8});
}
