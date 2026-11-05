#pragma once

#include "vwave/FftBackend.h"

#include <complex>
#include <memory>
#include <vector>

namespace vwave {

int nextPowerOfTwo(int value);

class FFT {
public:
    explicit FFT(int size);

    int size() const { return m_backend ? m_backend->size() : 0; }

    void forward(std::vector<std::complex<float>>& data) const;
    void forwardReal(const std::vector<float>& input,
                     std::vector<std::complex<float>>& output) const;
    void inverse(std::vector<std::complex<float>>& data) const;

private:
    std::unique_ptr<FftBackend> m_backend;
    std::unique_ptr<FftBackend> m_realBackend;
    std::vector<std::complex<float>> m_realTwiddles;
    mutable std::vector<std::complex<float>> m_realScratch;
};

void fftInPlace(float* real, float* imag, int size, bool inverse);
std::vector<float> hannWindow(int size);

} // namespace vwave
