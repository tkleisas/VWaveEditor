#include "test_framework.h"

#include "vwave/JobSystem.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace vwave;

VWTEST("job system executes tasks and reports progress") {
    JobSystem jobs(2);
    std::atomic<int> counter{0};
    JobHandle handle = jobs.submit([&](JobControl& control) {
        control.setProgress(0.25);
        ++counter;
        control.setProgress(1.0);
    });
    REQUIRE(handle.valid());
    handle.wait();
    handle.get();
    CHECK(handle.done());
    CHECK_EQ(counter.load(), 1);
    CHECK_NEAR(handle.progress(), 1.0, 1e-9);
    jobs.waitIdle();
}

VWTEST("job system supports cooperative cancellation") {
    JobSystem jobs(1);
    std::atomic<bool> started{false};
    JobHandle handle = jobs.submit([&](JobControl& control) {
        started.store(true);
        while (!control.cancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        control.setProgress(0.5);
    });
    while (!started.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(handle.cancel());
    handle.wait();
    handle.get();
    CHECK(handle.done());
    CHECK(handle.control()->cancelled());
    jobs.waitIdle();
}

VWTEST("job system propagates task exceptions") {
    JobSystem jobs(1);
    JobHandle handle = jobs.submit([](JobControl&) {
        throw std::runtime_error("job failed");
    });
    bool caught = false;
    try {
        handle.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(handle.done());
}

VWTEST("job system runs queued work concurrently and drains") {
    JobSystem jobs(3);
    std::atomic<int> completed{0};
    std::vector<JobHandle> handles;
    for (int i = 0; i < 12; ++i) {
        handles.push_back(jobs.submit([&](JobControl&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++completed;
        }));
    }
    jobs.waitIdle();
    CHECK_EQ(completed.load(), 12);
    for (auto& handle : handles) {
        handle.get();
        CHECK(handle.done());
    }
}
