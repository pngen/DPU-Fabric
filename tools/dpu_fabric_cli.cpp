// dpu_fabric_cli: command-line interface for DPU Fabric.
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/backend.h"
#include "dpufabric/types.h"
#include "dpufabric/eligibility.h"
#include "dpufabric/id.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

using namespace dpufabric;
using namespace dpufabric::synth;

namespace {
void print_usage() {
    std::printf("usage: dpu-fabric <discover|devices|capabilities|explain|plan|services|deployments|snapshot|validate-state|synthetic-demo>\n");
}
std::string arg_after(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == flag) return std::string(argv[i + 1]);
    return "";
}
}

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 0; }
    std::string cmd = argv[1];

    DPUFabricEngine engine(CoordinatorEpoch(1));
    engine.import_backend(make_synthetic_backend());
    engine.import_backend(make_system_backend());
    engine.import_backend(make_unsupported_backend());
    auto results = engine.run_all_backends();

    if (cmd == "discover") {
        for (auto& r : results) {
            std::printf("[%s] backend_available=%d physical_dpu_present=%d\n", r.backend_available ? "OK" : "MISSING", (int)r.backend_available, (int)r.physical_dpu_present);
            std::printf("  %s\n", r.summary.c_str());
            for (auto& d : r.devices) {
                std::printf("  [%s] %s %s class=%s\n", d.record.evidence_class_label(), d.record.model.c_str(), d.record.pci_reference.c_str(), device_class_name(d.record.device_class));
            }
        }
        std::printf("\nPhysical DPU execution: %s\n", ExecutionClass::UNSUPPORTED == ExecutionClass::UNSUPPORTED ? "UNSUPPORTED on this machine" : "present");
        return 0;
    }
    if (cmd == "devices") {
        auto snap = engine.snapshot();
        for (auto id : snap->device_order) {
            auto d = snap->device(id);
            if (!d) continue;
            std::printf("device %s [%s] %s (%s) lifecycle=%s caps=%zu\n", id.encode().c_str(), d->evidence_class_label(), d->model.c_str(), d->pci_reference.c_str(), device_lifecycle_name(d->lifecycle), d->capabilities.size());
        }
        return 0;
    }
    if (cmd == "capabilities") {
        auto snap = engine.snapshot();
        if (snap->devices.empty()) { std::printf("no devices\n"); return 0; }
        auto id = snap->device_order.front();
        auto d = snap->device(id);
        for (auto& [k, c] : d->capabilities.all()) {
            std::printf("  %s state=%s source=%s version=%llu\n", k.str().c_str(), capability_state_name(c.state), provenance_source_name(c.source), (unsigned long long)c.version);
        }
        return 0;
    }
    if (cmd == "explain" || cmd == "plan") {
        std::string svc = arg_after(argc, argv, "--service");
        OffloadClass oc = OffloadClass::SECURITY_SERVICE;
        if (svc == "encryption") oc = OffloadClass::SECURITY_SERVICE;
        else if (svc == "storage") oc = OffloadClass::STORAGE_SERVICE;
        else if (svc == "compression") oc = OffloadClass::COMPRESSION;
        ServiceDefinition sd;
        sd.service_id = ServiceId(100); sd.service_gen = ServiceGeneration(1);
        sd.offload_class = oc;
        if (oc == OffloadClass::SECURITY_SERVICE) { CapabilityRequirement cr; cr.key = cap_encryption(); sd.required_caps.push_back(cr); }
        if (oc == OffloadClass::STORAGE_SERVICE || oc == OffloadClass::COMPRESSION) { CapabilityRequirement cr; cr.key = cap_storage(); sd.required_caps.push_back(cr); }
        sd.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
        engine.register_service(sd);
        OffloadRequest req;
        req.offload_class = oc; req.service = sd.service_id; req.mode = OffloadMode::OFFLOAD_REQUIRED;
        if (oc == OffloadClass::COMPRESSION) {
            // No device supports compression on this backend set? B does.
        }
        auto res = engine.evaluate(req);
        std::printf("%s\n", res.summarize().c_str());
        std::printf("%s\n", res.explanation.c_str());
        return 0;
    }
    if (cmd == "services" || cmd == "deployments" || cmd == "snapshot") {
        auto snap = engine.snapshot();
        std::printf("epoch=%s devices=%zu services=%zu programs=%zu deployments=%zu activations=%zu\n",
            snap->epoch.encode().c_str(), snap->devices.size(), snap->services.size(),
            snap->programs.size(), snap->deployments.size(), snap->activations.size());
        return 0;
    }
    if (cmd == "validate-state") {
        auto snap = engine.snapshot();
        bool ok = true;
        for (auto& [id, dep] : snap->deployments) {
            if (dep.state == DeploymentState::ACTIVE && !snap->device(dep.device)) { std::printf("INVARIANT: active deployment %s has no device\n", id.encode().c_str()); ok = false; }
            if (dep.authoritative && dep.state != DeploymentState::ACTIVE) { std::printf("INVARIANT: deployment %s authoritative but not active\n", id.encode().c_str()); ok = false; }
        }
        std::printf("validate-state: %s\n", ok ? "OK (no invariant violation)" : "VIOLATION FOUND");
        return ok ? 0 : 1;
    }
    if (cmd == "synthetic-demo") {
        // Register the two-device service scenario, deploy, print.
        auto sid = ServiceId(101);
        ServiceDefinition sd;
        sd.service_id = sid; sd.service_gen = ServiceGeneration(1);
        sd.offload_class = OffloadClass::SECURITY_SERVICE;
        CapabilityRequirement cr; cr.key = cap_encryption(); sd.required_caps.push_back(cr);
        sd.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
        engine.register_service(sd);
        OffloadRequest req;
        req.offload_class = OffloadClass::SECURITY_SERVICE; req.service = sid; req.mode = OffloadMode::OFFLOAD_REQUIRED;
        auto elig = engine.evaluate(req);
        std::printf("selected=%s\n", elig.selected ? elig.selected->encode().c_str() : "NONE");
        if (elig.selected) {
            auto dep = engine.reserve_deployment(req, elig);
            engine.stage_deployment(dep); engine.load_deployment(dep); engine.validate_deployment(dep);
            engine.activate_deployment(dep); engine.commit_deployment(dep);
            std::printf("deployed=%s state=%s\n", dep.encode().c_str(), deployment_state_name(engine.snapshot()->deployment(dep)->state));
        } else {
            std::printf("no eligible device\n");
        }
        return 0;
    }
    print_usage();
    return 0;
}
