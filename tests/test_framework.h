#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace vwtest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Reg {
    Reg(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

class Failure : public std::runtime_error {
public:
    explicit Failure(const std::string& message) : std::runtime_error(message) {}
};

inline void fail(const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ':' << line << ": " << message;
    throw Failure(out.str());
}

inline int runAll() {
    int passed = 0;
    for (const auto& test : registry()) {
        failures() = 0;
        try {
            test.fn();
            ++passed;
            std::cout << "[ PASS ] " << test.name << '\n';
        } catch (const Failure& e) {
            std::cout << "[ FAIL ] " << test.name << "\n         " << e.what() << '\n';
        } catch (const std::exception& e) {
            std::cout << "[ FAIL ] " << test.name << "\n         unexpected exception: "
                      << e.what() << '\n';
        } catch (...) {
            std::cout << "[ FAIL ] " << test.name << "\n         unknown exception\n";
        }
    }
    std::cout << '\n' << passed << '/' << registry().size() << " tests passed\n";
    return passed == static_cast<int>(registry().size()) ? 0 : 1;
}

} // namespace vwtest

#define VWTEST_CONCAT_INNER(a, b) a##b
#define VWTEST_CONCAT(a, b) VWTEST_CONCAT_INNER(a, b)
#define VWTEST(name) \
    static void VWTEST_CONCAT(vwtest_fn_, __LINE__)(); \
    static vwtest::Reg VWTEST_CONCAT(vwtest_reg_, __LINE__)(name, VWTEST_CONCAT(vwtest_fn_, __LINE__)); \
    static void VWTEST_CONCAT(vwtest_fn_, __LINE__)()

#define CHECK(expr) \
    do { if (!(expr)) vwtest::fail(__FILE__, __LINE__, #expr); } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        const auto& vwtest_a = (actual); \
        const auto& vwtest_e = (expected); \
        if (!(vwtest_a == vwtest_e)) { \
            std::ostringstream vwtest_o; \
            vwtest_o << #actual << " == " << #expected << " (got " << vwtest_a \
                     << ", expected " << vwtest_e << ')'; \
            vwtest::fail(__FILE__, __LINE__, vwtest_o.str()); \
        } \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance) \
    do { \
        const double vwtest_a = static_cast<double>(actual); \
        const double vwtest_e = static_cast<double>(expected); \
        const double vwtest_t = static_cast<double>(tolerance); \
        if (!(std::abs(vwtest_a - vwtest_e) <= vwtest_t)) { \
            std::ostringstream vwtest_o; \
            vwtest_o << #actual << " near " << #expected << " (got " << vwtest_a \
                     << ", expected " << vwtest_e << ", tolerance " << vwtest_t << ')'; \
            vwtest::fail(__FILE__, __LINE__, vwtest_o.str()); \
        } \
    } while (0)

#define REQUIRE(expr) CHECK(expr)
