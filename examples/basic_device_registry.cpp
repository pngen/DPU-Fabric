#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    auto snap = e.snapshot();
    std::printf("basic_device_registry: epoch=%s devices=%zu\n", snap->epoch.encode().c_str(), snap->devices.size());
    std::printf("  registered device ids: ");
    for (auto id : snap->device_order) std::printf("%s ", id.encode().c_str());
    std::printf("\n");
    return 0;
}
