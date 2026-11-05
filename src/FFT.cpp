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

FFT::FFT(int size) : m_backend(makeFftBackend(size)) {
    if (size >= 2) {
        const int half = size / 2;
        m_realBackend = makeFftBackend(half);
        m_realTwiddles.resize(static_cast<size_t>(half + 1));
        for (int k = 0; k <= half; ++k) {
            const double angle = -2.0 * kPi * static_cast<double>(k) /
                                 static_cast<double>(size);
            m_realTwiddles[static_cast<size_t>(k)] = {
                static_cast<float>(std::cos(angle)),
                static_cast<float>(std::sin(angle))};
        }
    }
}

void FFT::forward(std::vector<std::complex<float>>& data) const {
    if (static_cast<int>(data.size()) != size())
        throw std::invalid_argument(
            "FFT data size does not match transform size");
    m_backend->forward(data.data());
}

void FFT::forwardReal(
    const std::vector<float>& input,
    std::vector<std::complex<float>>& output) const {
    if (static_cast<int>(input.size()) != size() ||
        static_cast<int>(output.size()) != size())
        throw std::invalid_argument(
            "FFT data size does not match transform size");
    const int n = size();
    if (n == 1) {
        output[0] = {input[0], 0.0f};
        return;
    }
    const int half = n / 2;
    m_realScratch.resize(static_cast<size_t>(half));
    for (int k = 0; k < half; ++k) {
        m_realScratch[static_cast<size_t>(k)] = {
            input[static_cast<size_t>(2 * k)],
            input[static_cast<size_t>(2 * k + 1)]};
    }
    m_realBackend->forward(m_realScratch.data());

    for (int k = 0; k <= half; ++k) {
        const auto zk = m_realScratch[static_cast<size_t>(k % half)];
        const auto zmk = std::conj(
            m_realScratch[static_cast<size_t>((half - k) % half)]);
        const auto even = 0.5f * (zk + zmk);
        const auto odd =
            std::complex<float>{0.0f, -0.5f} * (zk - zmk);
        output[static_cast<size_t>(k)] =
            even + m_realTwiddles[static_cast<size_t>(k)] * odd;
    }
    output[0] = {output[0].real(), 0.0f};
    output[static_cast<size_t>(half)] = {
        output[static_cast<size_t>(half)].real(), 0.0f};
    for (int k = half + 1; k < n; ++k)
        output[static_cast<size_t>(k)] =
            std::conj(output[static_cast<size_t>(n - k)]);
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
