#include <dpufabric/engine.h>
#include <dpufabric/synthetic.h>
#include <cstdio>
using namespace dpufabric; using namespace dpufabric::synth;
int main() {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    ServiceDefinition s; s.service_id = ServiceId(42); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 128u << 20, 2, 1, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = s.service_id; req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto elig = e.evaluate(req);
    auto dep = e.reserve_deployment(req, elig);
    e.stage_deployment(dep); e.load_deployment(dep); e.validate_deployment(dep); e.activate_deployment(dep); e.commit_deployment(dep);
    std::printf("stale_authority_rejection: before reboot authoritative=%d\n", (int)e.snapshot()->deployment(dep)->authoritative);
    e.fence_device(*elig.selected, "device rebooted");
    auto d = e.snapshot()->deployment(dep);
    std::printf("  after device reboot: state=%s authoritative=%d (no longer authoritative)\n",
        deployment_state_name(d->state), (int)d->authoritative);
    return 0;
}
