#pragma once

#include <complex>
#include <memory>

namespace vwave {

enum class FftBackendType {
    Radix2
};

class FftBackend {
public:
    virtual ~FftBackend() = default;

    virtual int size() const = 0;
    virtual void forward(std::complex<float>* data) const = 0;
    virtual void inverse(std::complex<float>* data) const = 0;
};

std::unique_ptr<FftBackend> makeFftBackend(
    int size, FftBackendType type = FftBackendType::Radix2);

} // namespace vwave
