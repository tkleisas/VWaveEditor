#pragma once

#include <complex>
#include <vector>

namespace vwave {

int nextPowerOfTwo(int value);

class FFT {
public:
    explicit FFT(int size);

    int size() const { return m_size; }

    void forward(std::vector<std::complex<float>>& data) const;
    void inverse(std::vector<std::complex<float>>& data) const;

private:
    int m_size = 0;
    std::vector<int> m_bitReverse;
    std::vector<std::complex<float>> m_twiddles;
};

void fftInPlace(float* real, float* imag, int size, bool inverse);
std::vector<float> hannWindow(int size);

} // namespace vwave
