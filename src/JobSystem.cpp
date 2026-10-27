#include "vwave/JobSystem.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace vwave {

struct JobSystem::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> tasks;
    std::vector<std::weak_ptr<JobControl>> controls;
    std::size_t active = 0;
    bool stopping = false;
};

JobSystem::JobSystem(std::size_t workerCount) : m_impl(std::make_unique<Impl>()) {
    if (workerCount == 0) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        workerCount = hardware > 1 ? static_cast<std::size_t>(hardware - 1)
                                   : std::size_t{1};
    }
    workerCount = std::max<std::size_t>(1, workerCount);
    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i)
        m_workers.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() {
    cancelAll();
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->wake.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
}

JobHandle JobSystem::submit(Task task) {
    if (!task) throw std::invalid_argument("job task is empty");

    auto control = std::make_shared<JobControl>();
    auto promise = std::make_shared<std::promise<void>>();
    std::future<void> future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->stopping)
            throw std::runtime_error("job system is stopping");
        m_impl->controls.erase(
            std::remove_if(
                m_impl->controls.begin(), m_impl->controls.end(),
                [](const std::weak_ptr<JobControl>& weak) {
                    auto control = weak.lock();
                    return !control || control->finished();
                }),
            m_impl->controls.end());
        m_impl->controls.push_back(control);
        m_impl->tasks.push_back(
            [task = std::move(task), control, promise]() mutable {
                try {
                    task(*control);
                    promise->set_value();
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
                control->markFinished();
            });
    }
    m_impl->wake.notify_one();
    return JobHandle(std::move(future), std::move(control));
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lock(m_impl->mutex);
    m_impl->wake.wait(lock, [this] {
        return m_impl->tasks.empty() && m_impl->active == 0;
    });
}

void JobSystem::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_impl->mutex);
            m_impl->wake.wait(lock, [this] {
                return m_impl->stopping || !m_impl->tasks.empty();
            });
            if (m_impl->tasks.empty()) {
                if (m_impl->stopping) return;
                continue;
            }
            task = std::move(m_impl->tasks.front());
            m_impl->tasks.pop_front();
            ++m_impl->active;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            --m_impl->active;
        }
        m_impl->wake.notify_all();
    }
}

void JobSystem::cancelAll() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& weak : m_impl->controls) {
        if (auto control = weak.lock()) control->cancel();
    }
    m_impl->controls.erase(
        std::remove_if(m_impl->controls.begin(), m_impl->controls.end(),
                       [](const std::weak_ptr<JobControl>& weak) {
                           return weak.expired();
                       }),
        m_impl->controls.end());
}

} // namespace vwave
