#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <dpufabric/eligibility.h>
#include <dpufabric/persistence.h>
#include <cstdio>
#include <chrono>
#include <string>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    ServiceDefinition s; s.service_id = ServiceId(1); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::SECURITY_SERVICE;
    CapabilityRequirement c; c.key = cap_encryption(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::SECURITY_SERVICE; req.service = ServiceId(1); req.mode = OffloadMode::OFFLOAD_REQUIRED;

    const int N = 100000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) { auto s = e.snapshot(); (void)s->devices.size(); }
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) { auto r = e.evaluate(req); (void)r.selected; }
    auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) { auto s = e.snapshot(); auto x = (void)0; (void)x; }
    auto t3 = std::chrono::steady_clock::now();

    auto d0 = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    auto d1 = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    auto d2 = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();
    std::printf("benchmark (Release):\n");
    std::printf("  snapshot lookup : %d ops in %lld ns -> %.0f ops/sec\n", N, (long long)d0, (double)N * 1e9 / (double)d0);
    std::printf("  offload evaluate: %d ops in %lld ns -> %.0f ops/sec\n", N, (long long)d1, (double)N * 1e9 / (double)d1);
    std::printf("  snapshot metadata: %d ops in %lld ns -> %.0f ops/sec\n", N, (long long)d2, (double)N * 1e9 / (double)d2);
    return 0;
}
