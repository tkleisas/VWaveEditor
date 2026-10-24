#include "test_framework.h"

#include "vwave/FFT.h"

#include <cmath>
#include <complex>
#include <vector>

using namespace vwave;

VWTEST("fft next power of two") {
    CHECK_EQ(nextPowerOfTwo(1), 1);
    CHECK_EQ(nextPowerOfTwo(2), 2);
    CHECK_EQ(nextPowerOfTwo(3), 4);
    CHECK_EQ(nextPowerOfTwo(1024), 1024);
    CHECK_EQ(nextPowerOfTwo(1025), 2048);
    CHECK_EQ(nextPowerOfTwo(0), 1);
}

VWTEST("fft impulse has a flat spectrum") {
    FFT fft(8);
    std::vector<std::complex<float>> data(8, {0.0f, 0.0f});
    data[0] = {1.0f, 0.0f};
    fft.forward(data);
    for (const auto& value : data) {
        CHECK_NEAR(value.real(), 1.0, 1e-5);
        CHECK_NEAR(value.imag(), 0.0, 1e-5);
    }
}

VWTEST("fft resolves a sine bin") {
    FFT fft(64);
    std::vector<std::complex<float>> data(64, {0.0f, 0.0f});
    for (int i = 0; i < 64; ++i) {
        data[static_cast<size_t>(i)] = {
            static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 5.0 * i / 64.0)),
            0.0f};
    }
    fft.forward(data);
    size_t peak = 0;
    float peakMagnitude = 0.0f;
    for (size_t i = 0; i < data.size(); ++i) {
        const float magnitude = std::abs(data[i]);
        if (magnitude > peakMagnitude) {
            peakMagnitude = magnitude;
            peak = i;
        }
    }
    CHECK_EQ(peak, size_t{5});
    CHECK_NEAR(peakMagnitude, 32.0, 0.01);
}

VWTEST("fft inverse round-trip") {
    FFT fft(128);
    std::vector<std::complex<float>> original(128);
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = {static_cast<float>(std::sin(i * 0.17)),
                       static_cast<float>(std::cos(i * 0.31))};
    }
    auto data = original;
    fft.forward(data);
    fft.inverse(data);
    for (size_t i = 0; i < data.size(); ++i) {
        CHECK_NEAR(data[i].real(), original[i].real(), 1e-4);
        CHECK_NEAR(data[i].imag(), original[i].imag(), 1e-4);
    }
}

VWTEST("fft rejects non-power-of-two sizes") {
    bool threw = false;
    try {
        FFT fft(100);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}
