#pragma once

#include <functional>
#include <string>
#include <vector>
#include <iostream>

namespace ashgrove_test {

inline int& failures() { static int f = 0; return f; }
inline int& passes() { static int p = 0; return p; }

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, fn});
    }
};

inline int run_all() {
    int failed = 0;
    for (auto& test : registry()) {
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << test.name << ": " << e.what() << std::endl;
            failed++;
        }
    }
    return failed;
}

#define TEST(name) \
    static void name(); \
    static ashgrove_test::Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) throw std::runtime_error("CHECK failed: " #cond); } while (0)

#define CHECK_EQ(a, b) \
    do { if (!((a) == (b))) throw std::runtime_error("CHECK_EQ failed: " #a " == " #b); } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { if (!(((a) - (b) < (eps)) && ((b) - (a) < (eps)))) throw std::runtime_error("CHECK_NEAR failed"); } while (0)

} // namespace ashgrove_test