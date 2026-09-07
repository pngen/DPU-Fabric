#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    auto snap = e.snapshot();
    auto a = snap->device(DeviceId(1));
    std::printf("capability_query: DPU-A ENCRYPTION=%s STORAGE=%s\n",
        a->capabilities.authoritative(cap_encryption()) ? "SUPPORTED" : "UNSUPPORTED",
        a->capabilities.authoritative(cap_storage()) ? "SUPPORTED" : "UNSUPPORTED");
    return 0;
}
