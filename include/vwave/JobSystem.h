#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace vwave {

class JobControl {
public:
    bool cancelled() const noexcept {
        return m_cancelled.load(std::memory_order_acquire);
    }
    void cancel() noexcept {
        m_cancelled.store(true, std::memory_order_release);
    }
    double progress() const noexcept {
        return static_cast<double>(
            m_progress.load(std::memory_order_acquire));
    }
    void setProgress(double value) noexcept {
        if (value < 0.0) value = 0.0;
        if (value > 1.0) value = 1.0;
        m_progress.store(static_cast<float>(value),
                         std::memory_order_release);
    }
    bool finished() const noexcept {
        return m_finished.load(std::memory_order_acquire);
    }

private:
    friend class JobSystem;
    void markFinished() noexcept {
        m_finished.store(true, std::memory_order_release);
    }

    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_finished{false};
    std::atomic<float> m_progress{0.0f};
};

class JobHandle {
public:
    JobHandle() = default;
    JobHandle(JobHandle&&) noexcept = default;
    JobHandle& operator=(JobHandle&&) noexcept = default;
    JobHandle(const JobHandle&) = delete;
    JobHandle& operator=(const JobHandle&) = delete;

    bool valid() const { return m_future.valid(); }
    bool done() const {
        return !m_control || m_control->finished();
    }
    bool cancel() {
        if (!m_control || done()) return false;
        m_control->cancel();
        return true;
    }
    double progress() const {
        return m_control ? m_control->progress() : 1.0;
    }
    void wait() const {
        if (m_future.valid()) m_future.wait();
    }
    void get() {
        if (m_future.valid()) m_future.get();
    }
    const std::shared_ptr<JobControl>& control() const { return m_control; }

private:
    friend class JobSystem;
    JobHandle(std::future<void> future, std::shared_ptr<JobControl> control)
        : m_future(std::move(future)), m_control(std::move(control)) {}

    std::future<void> m_future;
    std::shared_ptr<JobControl> m_control;
};

class JobSystem {
public:
    explicit JobSystem(std::size_t workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    using Task = std::function<void(JobControl&)>;
    JobHandle submit(Task task);
    void waitIdle();
    std::size_t workerCount() const { return m_workers.size(); }

private:
    void workerLoop();
    void cancelAll();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<std::thread> m_workers;
};

} // namespace vwave
