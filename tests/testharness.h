// Minimal self-contained test harness for DPU Fabric.
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <exception>
#include "dpufabric/error.h"

namespace dputest {
struct TestCase { std::string name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Registrar { Registrar(std::string n, std::function<void()> f) { registry().push_back({std::move(n), std::move(f)}); } };
inline void check(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::string msg = std::string(file) + ":" + std::to_string(line) + " check failed: " + expr;
        throw std::runtime_error(msg);
    }
}

inline int run_all() {
    int fails = 0;
    for (auto& t : registry()) {
        try { t.fn(); std::printf("[PASS] %s\n", t.name.c_str()); }
        catch (const std::exception& e) { std::printf("[FAIL] %s: %s\n", t.name.c_str(), e.what()); ++fails; }
        catch (...) { std::printf("[FAIL] %s: unknown exception\n", t.name.c_str()); ++fails; }
        std::fflush(stdout);
    }
    std::printf("TOTAL: %zu cases, %d failures\n", registry().size(), fails);
    std::fflush(stdout);
    return fails;
}
} // namespace dputest

#define DPUFABRIC_TEST(name) \
    static void name(); \
    static dputest::Registrar dpu_reg_##name(#name, name); \
    static void name()

#define CHECK(c) dputest::check((c), #c, __FILE__, __LINE__)

// Helper: assert that expression throws a DpuError with the given code.
template <typename Fn>
inline void dpu_expect_throw_code(Fn&& fn, dpufabric::ErrorCode code) {
    try { fn(); }
    catch (const dpufabric::DpuError& e) {
        if (e.code() != (code)) throw std::runtime_error("expected different error code");
        return;
    }
    catch (...) { throw std::runtime_error("expected DpuError but got other exception"); }
    throw std::runtime_error("expected DpuError but none thrown");
}

#define CHECK_THROWS_CODE(expr, code) dpu_expect_throw_code([&]{ expr; }, (code))

#define DPUFABRIC_MAIN \
    int main() { return dputest::run_all(); }
