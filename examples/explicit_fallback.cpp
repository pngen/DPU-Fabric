#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <dpufabric/eligibility.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    OffloadRequest req; req.offload_class = OffloadClass::VIRTUAL_SWITCHING; req.mode = OffloadMode::OFFLOAD_PREFERRED; req.allow_host_fallback = true;
    auto res = e.evaluate(req);
    std::printf("explicit_fallback: execution=%s fallback=%s reason=%s\n",
        execution_class_name(res.execution), res.fallback_used ? "yes" : "no", res.fallback_reason.c_str());
    return 0;
}
