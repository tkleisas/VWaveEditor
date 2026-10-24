#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("vwave_test_dir_" + std::to_string(stamp) + "_" +
                 std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }
    std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

class TempPath {
public:
    explicit TempPath(const std::string& suffix) {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto name = "vwave_test_" + std::to_string(stamp) + "_" +
                    std::to_string(counter.fetch_add(1)) + suffix;
        path_ = std::filesystem::temp_directory_path() / name;
    }

    ~TempPath() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }
    std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};
