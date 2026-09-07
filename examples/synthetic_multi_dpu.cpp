#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <dpufabric/eligibility.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    for (auto oc : { OffloadClass::SECURITY_SERVICE, OffloadClass::STORAGE_SERVICE }) {
        OffloadRequest req; req.offload_class = oc; req.mode = OffloadMode::OFFLOAD_REQUIRED;
        if (oc == OffloadClass::SECURITY_SERVICE) { CapabilityRequirement c; c.key = cap_encryption(); req.required_caps.push_back(c); }
        else { CapabilityRequirement c; c.key = cap_storage(); req.required_caps.push_back(c); }
        auto res = e.evaluate(req);
        std::printf("synthetic_multi_dpu: %s -> %s\n", offload_class_name(oc), res.selected ? res.selected->encode().c_str() : "NONE");
    }
    return 0;
}
