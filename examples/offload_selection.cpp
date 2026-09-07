#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <dpufabric/eligibility.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    OffloadRequest req; req.offload_class = OffloadClass::SECURITY_SERVICE; req.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement c; c.key = cap_encryption(); req.required_caps.push_back(c);
    auto res = e.evaluate(req);
    std::printf("offload_selection: %s\n", res.summarize().c_str());
    return 0;
}
