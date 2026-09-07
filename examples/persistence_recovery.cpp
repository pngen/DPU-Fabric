#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    const char* path = "example_state.dpu";
    {
        DPUFabricEngine e(CoordinatorEpoch(1));
        for (auto& d : scenario_ab()) e.register_device(d.record);
        e.persist(path);
    }
    {
        DPUFabricEngine e(CoordinatorEpoch(1));
        bool existed = e.recover_from(path);
        auto snap = e.snapshot();
        std::printf("persistence_recovery: existed=%d devices=%zu epoch=%s\n", (int)existed, snap->devices.size(), snap->epoch.encode().c_str());
        for (auto& [id, d] : snap->devices) std::printf("  device %s evidence_fresh=%d\n", id.encode().c_str(), (int)d.evidence_fresh);
    }
    std::remove(path);
    return 0;
}
