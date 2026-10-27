#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace vwave::app {

class UiDispatcher {
public:
    void post(std::function<void()> task) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }

    void drain() {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            tasks.swap(m_tasks);
        }
        for (auto& task : tasks) task();
    }

    std::size_t pending() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tasks.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::function<void()>> m_tasks;
};

} // namespace vwave::app
