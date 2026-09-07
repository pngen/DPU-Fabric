// Real OS-process worker-death / restart proof.
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
}

DPUFABRIC_TEST(mp_worker_death_and_restart) {
    uint16_t port = mptest::find_free_port();
    CHECK(port != 0);
    std::string tools = mptest::tools_dir();

    mptest::Proc coord = mptest::spawn(tools + "/dpu_fabric_coordinator.exe",
        "--port " + std::to_string(port) + " --epoch 1");
    CHECK(coord.h != nullptr);
    CHECK(mptest::wait_port(port, 8000));

    mptest::Proc wa = mptest::spawn(tools + "/dpu_fabric_worker.exe",
        "--port " + std::to_string(port) + " --worker-id 1 --boot-id 100 --label A");
    CHECK(wa.h != nullptr);

    CHECK(poll_create_ok(port, 40, 200));
    std::printf("[mp] initial deployment established\n");

    // Kill Worker A as a real OS process.
    CHECK(wa.kill());

    // The coordinator must fence the affected device; a new deployment must fail.
    CHECK(poll_create_fail(port, 40, 200));
    std::printf("[mp] device fenced after worker death (deployment rejected)\n");

    // A new Worker A process with a fresh boot id re-registers and revalidates.
    mptest::Proc wa2 = mptest::spawn(tools + "/dpu_fabric_worker.exe",
        "--port " + std::to_string(port) + " --worker-id 1 --boot-id 200 --label A2");
    CHECK(wa2.h != nullptr);
    CHECK(poll_create_ok(port, 40, 200));
    std::printf("[mp] fresh authority established after worker restart\n");

    // A stale worker claiming the OLD boot id must be rejected by the coordinator.
    mptest::Proc old = mptest::spawn(tools + "/dpu_fabric_worker.exe",
        "--port " + std::to_string(port) + " --worker-id 1 --boot-id 100 --label stale");
    CHECK(old.h != nullptr);
    int ec = old.wait_exit(6000);
    std::printf("[mp] stale boot worker exit code = %d\n", ec);
    CHECK(ec != 0);

    wa2.kill();
    coord.kill();
}

DPUFABRIC_MAIN
