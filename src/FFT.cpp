#include "vwave/FFT.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vwave {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

int nextPowerOfTwo(int value) {
    if (value <= 1) return 1;
    unsigned int v = static_cast<unsigned int>(value - 1);
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return static_cast<int>(v + 1);
}

namespace {

class Radix2FftBackend final : public FftBackend {
public:
    explicit Radix2FftBackend(int size) : m_size(size) {
        if (size <= 0 || (size & (size - 1)) != 0)
            throw std::invalid_argument(
                "FFT size must be a positive power of two");
        m_bitReverse.resize(static_cast<size_t>(size));
        int bits = 0;
        while ((1 << bits) < size) ++bits;
        for (int i = 0; i < size; ++i) {
            int reversed = 0;
            for (int bit = 0; bit < bits; ++bit) {
                if (i & (1 << bit)) reversed |= 1 << (bits - 1 - bit);
            }
            m_bitReverse[static_cast<size_t>(i)] = reversed;
        }
        m_twiddles.resize(static_cast<size_t>(size / 2));
        for (int i = 0; i < size / 2; ++i) {
            const double angle = -2.0 * kPi * static_cast<double>(i) /
                                 static_cast<double>(size);
            m_twiddles[static_cast<size_t>(i)] = {
                static_cast<float>(std::cos(angle)),
                static_cast<float>(std::sin(angle))};
        }
    }

    int size() const override { return m_size; }

    void forward(std::complex<float>* data) const override {
        for (int i = 0; i < m_size; ++i) {
            const int j = m_bitReverse[static_cast<size_t>(i)];
            if (j > i) std::swap(data[i], data[j]);
        }
        for (int length = 2; length <= m_size; length <<= 1) {
            const int half = length / 2;
            const int stride = m_size / length;
            for (int start = 0; start < m_size; start += length) {
                for (int j = 0; j < half; ++j) {
                    const auto w = m_twiddles[static_cast<size_t>(j * stride)];
                    const auto even = data[start + j];
                    const auto odd = data[start + j + half] * w;
                    data[start + j] = even + odd;
                    data[start + j + half] = even - odd;
                }
            }
        }
    }

    void inverse(std::complex<float>* data) const override {
        for (int i = 0; i < m_size; ++i)
            data[i] = std::conj(data[i]);
        forward(data);
        const float scale = 1.0f / static_cast<float>(m_size);
        for (int i = 0; i < m_size; ++i)
            data[i] = std::conj(data[i]) * scale;
    }

private:
    int m_size = 0;
    std::vector<int> m_bitReverse;
    std::vector<std::complex<float>> m_twiddles;
};

} // namespace

std::unique_ptr<FftBackend> makeFftBackend(int size, FftBackendType type) {
    switch (type) {
    case FftBackendType::Radix2:
        return std::make_unique<Radix2FftBackend>(size);
    }
    throw std::invalid_argument("unknown FFT backend");
}

FFT::FFT(int size) : m_backend(makeFftBackend(size)) {}

void FFT::forward(std::vector<std::complex<float>>& data) const {
    if (static_cast<int>(data.size()) != size())
        throw std::invalid_argument(
            "FFT data size does not match transform size");
    m_backend->forward(data.data());
}

void FFT::inverse(std::vector<std::complex<float>>& data) const {
    if (static_cast<int>(data.size()) != size())
        throw std::invalid_argument(
            "FFT data size does not match transform size");
    m_backend->inverse(data.data());
}

void fftInPlace(float* real, float* imag, int size, bool inverse) {
    if (size <= 0 || real == nullptr || imag == nullptr) return;
    if ((size & (size - 1)) != 0) return;
    std::vector<std::complex<float>> data(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
        data[static_cast<size_t>(i)] = {real[i], imag[i]};
    FFT fft(size);
    if (inverse) fft.inverse(data);
    else fft.forward(data);
    for (int i = 0; i < size; ++i) {
        real[i] = data[static_cast<size_t>(i)].real();
        imag[i] = data[static_cast<size_t>(i)].imag();
    }
}

std::vector<float> hannWindow(int size) {
    std::vector<float> window(static_cast<size_t>(std::max(0, size)), 0.0f);
    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }
    for (int i = 0; i < size; ++i) {
        window[static_cast<size_t>(i)] = static_cast<float>(
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                 static_cast<double>(size)));
    }
    return window;
}

} // namespace vwave
