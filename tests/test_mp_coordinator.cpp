// Real OS-process coordinator death / restart proof.
#include "testharness.h"
#include "mp_harness.h"
#include "dpufabric/types.h"
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>

using namespace dpufabric;

namespace {
bool poll_create_ok(uint16_t port, int attempts, int ms_each) {
    for (int i = 0; i < attempts; ++i) {
        auto r = mptest::admin_create(port, 90, static_cast<int>(OffloadClass::SECURITY_SERVICE));
        if (r.rfind("OK:", 0) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_each));
    }
    return false;
}
bool poll_create_fail(uint16_t port, int attempts, int ms_each) {
    for (int i = 0; i < attempts; ++i) {
        auto r = mptest::admin_create(port, 90, static_cast<int>(OffloadClass::SECURITY_SERVICE));
        if (r.rfind("ERR:", 0) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_each));
    }
    return false;
}
std::string status(uint16_t port) { return mptest::admin(port, 2, {}); }
}

DPUFABRIC_TEST(mp_coordinator_death_and_restart) {
    uint16_t port = mptest::find_free_port();
    CHECK(port != 0);
    std::string tools = mptest::tools_dir();
    std::string state = "tmp_coord_state_" + std::to_string(port) + ".dpu";

    mptest::Proc c1 = mptest::spawn(tools + "/dpu_fabric_coordinator.exe",
        "--port " + std::to_string(port) + " --epoch 5 --state " + state);
    CHECK(c1.h != nullptr);
    CHECK(mptest::wait_port(port, 8000));

    mptest::Proc wa = mptest::spawn(tools + "/dpu_fabric_worker.exe",
        "--port " + std::to_string(port) + " --worker-id 5 --boot-id 100 --label A");
    CHECK(wa.h != nullptr);
    CHECK(poll_create_ok(port, 40, 200));
    std::printf("[mp] epoch-5 authority established\n");

    auto sr = mptest::admin(port, 4, {});
    std::printf("[mp] shutdown result: %s\n", sr.c_str());
    CHECK(sr.rfind("OK:", 0) == 0);
    int c1code = c1.wait_exit(8000);
    std::printf("[mp] coordinator#1 exit=%d\n", c1code);
    CHECK(c1code == 0);
    wa.kill();

    mptest::Proc c2 = mptest::spawn(tools + "/dpu_fabric_coordinator.exe",
        "--port " + std::to_string(port) + " --epoch 10 --state " + state);
    CHECK(c2.h != nullptr);
    CHECK(mptest::wait_port(port, 8000));

    std::string st = status(port);
    std::printf("[mp] post-recovery status: %s\n", st.c_str());
    CHECK(st.find("0000000000000006") != std::string::npos);

    CHECK(poll_create_fail(port, 20, 200));
    std::printf("[mp] recovered device evidence not fresh until revalidation\n");

    mptest::Proc wb = mptest::spawn(tools + "/dpu_fabric_worker.exe",
        "--port " + std::to_string(port) + " --worker-id 5 --boot-id 300 --label B");
    CHECK(wb.h != nullptr);
    CHECK(poll_create_ok(port, 40, 200));
    std::printf("[mp] fresh authority established under epoch 6\n");

    CHECK(poll_create_ok(port, 10, 200));

    wb.kill();
    c2.kill();
    std::remove(state.c_str());
}

DPUFABRIC_MAIN
