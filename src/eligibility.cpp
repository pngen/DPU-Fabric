
#include "dpufabric/eligibility.h"
#include "dpufabric/registry.h"
#include "dpufabric/request.h"
#include "dpufabric/device.h"
#include "dpufabric/program.h"
#include "dpufabric/deployment.h"
#include "dpufabric/resource.h"
#include <algorithm>
#include <string>
#include <limits>

namespace dpufabric {

namespace {

int health_severity(DeviceHealth h) noexcept {
    switch (h) {
        case DeviceHealth::HEALTHY: return 1;
        case DeviceHealth::WARN: return 2;
        case DeviceHealth::DEGRADED: return 3;
        case DeviceHealth::FAILED: return 4;
        case DeviceHealth::UNKNOWN: return 5;
        case DeviceHealth::END_MARKER: return 5;
    }
    return 5;
}

double locality_value(LocalityQuality q) noexcept {
    switch (q) {
        case LocalityQuality::SAME_DEVICE: return 1.0;
        case LocalityQuality::SAME_FUNCTION: return 0.9;
        case LocalityQuality::SAME_NIC: return 0.8;
        case LocalityQuality::SAME_ROOT: return 0.6;
        case LocalityQuality::REMOTE: return 0.3;
        case LocalityQuality::UNKNOWN: return 0.2;
        case LocalityQuality::END_MARKER: return 0.2;
    }
    return 0.2;
}

bool health_meets_policy(DeviceHealth h, DeviceHealth min) noexcept {
    return health_severity(h) <= health_severity(min);
}

bool has_port(const DeviceRecord& d, PortId p) noexcept {
    for (auto& x : d.ports) if (x == p) return true;
    return false;
}
bool has_function(const DeviceRecord& d, FunctionId f) noexcept {
    for (auto& x : d.functions) if (x == f) return true;
    return false;
}

double queue_pressure(const DeviceRecord& d, const ResourceLedger::Usage& u) noexcept {
    uint64_t cap = d.resource_capacity.queues;
    if (cap == 0) return 0.0;
    if (u.queues >= cap) return 1.0;
    return static_cast<double>(u.queues) / static_cast<double>(cap);
}

double device_load(const DeviceRecord& d, const ResourceLedger::Usage& u) noexcept {
    uint64_t slots = d.resource_capacity.slots;
    if (slots == 0) return 0.5;
    if (u.slots >= slots) return 1.0;
    return static_cast<double>(u.slots) / static_cast<double>(slots);
}

double headroom_pct(const DeviceRecord& d, const ResourceLedger::Usage& u) noexcept {
    auto frac = [](uint64_t used, uint64_t cap) -> double {
        if (cap == 0) return 1.0;
        if (used >= cap) return 0.0;
        return static_cast<double>(cap - used) / static_cast<double>(cap);
    };
    double h = 1.0;
    h = std::min(h, frac(u.memory_bytes, d.resource_capacity.memory_bytes));
    h = std::min(h, frac(u.queues, d.resource_capacity.queues));
    h = std::min(h, frac(u.functions, d.resource_capacity.functions));
    h = std::min(h, frac(u.slots, d.resource_capacity.slots));
    h = std::min(h, frac(u.contexts, d.resource_capacity.contexts));
    return h * 100.0;
}

const ProgramArtifact* find_current_program(const Snapshot& snap, ProgramId pid, ProgramGeneration gen) noexcept {
    auto git = snap.programs.find(pid);
    if (git == snap.programs.end()) return nullptr;
    auto& gens = git->second;
    auto fit = gens.find(gen);
    return fit == gens.end() ? nullptr : &fit->second;
}

bool can_admit(const ResourceProfile& cap, const ResourceLedger::Usage& u, const ResourceRequest& r) noexcept {
    if (cap.memory_bytes && (u.memory_bytes + r.memory_bytes > cap.memory_bytes)) return false;
    if (cap.queues && (u.queues + r.queues > cap.queues)) return false;
    if (cap.functions && (u.functions + r.functions > cap.functions)) return false;
    if (cap.slots && (u.slots + r.slots > cap.slots)) return false;
    if (cap.contexts && (u.contexts + r.contexts > cap.contexts)) return false;
    return true;
}

size_t device_order_index(const Snapshot& snap, DeviceId id) noexcept {
    for (size_t i = 0; i < snap.device_order.size(); ++i) if (snap.device_order[i] == id) return i;
    return std::numeric_limits<size_t>::max();
}

void collect_required_caps(const OffloadRequest& req, const ServiceDefinition* svc, const ProgramArtifact* prog,
                           std::vector<CapabilityRequirement>& out) {
    out.insert(out.end(), req.required_caps.begin(), req.required_caps.end());
    if (svc) {
        out.insert(out.end(), svc->required_caps.begin(), svc->required_caps.end());
        out.insert(out.end(), svc->optional_caps.begin(), svc->optional_caps.end());
    }
    if (prog) out.insert(out.end(), prog->required_caps.begin(), prog->required_caps.end());
}

std::string build_explanation(const EligibilityResult& r, const Snapshot& snap) {
    std::string s;
    if (r.selected) {
        const DeviceRecord* dev = snap.device(*r.selected);
        s = "SELECTED: hard_constraints=PASS ";
        for (auto& cand : r.candidates) {
            if (cand.device == *r.selected) {
                for (auto& fs : cand.scores) {
                    s += std::string(rank_factor_name(fs.factor)) + "=";
                    s += (fs.factor == RankFactor::WARM_SERVICE) ? (fs.normalized > 0.5 ? "YES" : "NO") : std::to_string((int)(fs.normalized * 100));
                    s += " ";
                }
                break;
            }
        }
        if (dev) s += "device=" + dev->model;
        s += " fallback_not_required";
    } else {
        s = "REJECTED:";
        for (auto gr : r.global_rejections) { s += " "; s += rejection_reason_name(gr); }
        // Summarize the most common per-candidate rejections.
        std::vector<RejectionReason> agg;
        for (auto& c : r.candidates) {
            for (auto rr : c.rejected) {
                bool seen = false;
                for (auto x : agg) if (x == rr) { seen = true; break; }
                if (!seen) agg.push_back(rr);
            }
        }
        for (auto rr : agg) { s += " "; s += rejection_reason_name(rr); }
    }
    return s;
}

} // namespace

EligibilityResult evaluate_offload(const Snapshot& snap, const OffloadRequest& req) {
    EligibilityResult result;
    result.mode = req.mode;
    const auto& policy = snap.policy;

    if (req.min_headroom > 100) throw_error(ErrorCode::INVALID_ARGUMENT, "min_headroom out of range");
    if (req.max_queue_pressure > 100) throw_error(ErrorCode::INVALID_ARGUMENT, "max_queue_pressure out of range");

    const ServiceDefinition* svc = nullptr;
    if (req.service) svc = snap.service(*req.service);

    std::optional<ProgramId> wanted_program = req.required_program;
    if (svc && !svc->required_program.is_null()) wanted_program = svc->required_program;

    std::vector<CandidateReport> passed;
    std::vector<CandidateReport> all;

    for (DeviceId did : snap.device_order) {
        const DeviceRecord* dev = snap.device(did);
        if (!dev) continue;
        CandidateReport rep;
        rep.device = did;
        rep.detail = std::string("device=") + dev->model + " / " + dev->pci_reference;

        if (req.required_device_class && *req.required_device_class != dev->device_class)
            rep.rejected.push_back(RejectionReason::DEVICE_CLASS_MISMATCH);
        if (req.pinned_device && *req.pinned_device != did)
            rep.rejected.push_back(RejectionReason::PINNED_DEVICE_MISMATCH);

        if (dev->lifecycle == DeviceLifecycle::DRAINING) rep.rejected.push_back(RejectionReason::DEVICE_DRAINING);
        else if (dev->lifecycle == DeviceLifecycle::FAILED) rep.rejected.push_back(RejectionReason::DEVICE_FAILED);
        else if (dev->lifecycle == DeviceLifecycle::REVALIDATION_REQUIRED || !dev->evidence_fresh)
            rep.rejected.push_back(RejectionReason::DEVICE_EVIDENCE_STALE);
        else if (dev->lifecycle != DeviceLifecycle::READY)
            rep.rejected.push_back(RejectionReason::DEVICE_NOT_READY);

        if (dev->lifecycle == DeviceLifecycle::READY && !health_meets_policy(dev->health, policy.min_device_health))
            rep.rejected.push_back(RejectionReason::DEVICE_HEALTH_THRESHOLD);

        if (req.offload_class != OffloadClass::CUSTOM_INFRASTRUCTURE_SERVICE && !dev->supports_class(req.offload_class))
            rep.rejected.push_back(RejectionReason::REQUIRED_CAPABILITY_UNSUPPORTED);

        const ProgramArtifact* prog = nullptr;
        if (wanted_program) {
            ProgramGeneration cur = snap.current_gen(*wanted_program);
            prog = find_current_program(snap, *wanted_program, cur);
            if (!prog) {
                rep.rejected.push_back(RejectionReason::ARTIFACT_INCOMPATIBLE);
            } else if (prog->program_gen != cur) {
                rep.rejected.push_back(RejectionReason::PROGRAM_GENERATION_STALE);
            } else {
                if (!dev->architecture.empty() && !prog->target_architecture.empty() &&
                    dev->architecture != prog->target_architecture && prog->target_architecture != "any")
                    rep.rejected.push_back(RejectionReason::ARCHITECTURE_INCOMPATIBLE);
                if (!req.required_abi.empty() && !prog->runtime_abi.empty() && prog->runtime_abi != req.required_abi)
                    rep.rejected.push_back(RejectionReason::ABI_INCOMPATIBLE);
            }
        }

        uint64_t fw_floor = 0;
        if (svc) fw_floor = std::max(fw_floor, svc->min_firmware_gen.as_u64());
        if (prog) fw_floor = std::max(fw_floor, prog->min_firmware_gen.as_u64());
        if (dev->firmware_gen.as_u64() < fw_floor) rep.rejected.push_back(RejectionReason::FIRMWARE_INCOMPATIBLE);
        uint64_t rt_floor = 0;
        if (svc) rt_floor = std::max(rt_floor, svc->min_runtime_gen.as_u64());
        if (dev->runtime_gen.as_u64() < rt_floor) rep.rejected.push_back(RejectionReason::RUNTIME_INCOMPATIBLE);

        std::vector<CapabilityRequirement> reqcaps;
        collect_required_caps(req, svc, prog, reqcaps);
        for (auto& c : reqcaps) {
            if (c.optional) continue;
            CapabilityState st = dev->capabilities.state_of(c.key);
            if (st == CapabilityState::SUPPORTED) {
                const Capability* cp = dev->capabilities.find(c.key);
                if (cp && cp->version < c.min_version) rep.rejected.push_back(RejectionReason::CAPABILITY_VERSION_MISMATCH);
            } else if (st == CapabilityState::UNKNOWN) {
                rep.rejected.push_back(RejectionReason::REQUIRED_CAPABILITY_UNKNOWN);
            } else if (st == CapabilityState::REVALIDATION_REQUIRED) {
                rep.rejected.push_back(RejectionReason::REQUIRED_CAPABILITY_STALE);
            } else {
                rep.rejected.push_back(RejectionReason::REQUIRED_CAPABILITY_UNSUPPORTED);
            }
        }
        for (auto& c : policy.hard_caps) {
            if (dev->capabilities.state_of(c.key) != CapabilityState::SUPPORTED)
                rep.rejected.push_back(RejectionReason::REQUIRED_CAPABILITY_UNSUPPORTED);
        }

        const ResourceLedger::Usage* usage = snap.usage(did);
        ResourceLedger::Usage zero{};
        const ResourceLedger::Usage& u = usage ? *usage : zero;

        bool iso_proven = dev->hardware_isolation && dev->isolation_proven && dev->evidence_fresh;
        IsolationSpec iso = svc ? svc->isolation : req.isolation;
        if (iso.level != IsolationRequirement::NONE) {
            if (!iso.satisfied_by(iso_proven, dev->isolation_domain, dev->tenant))
                rep.rejected.push_back(RejectionReason::ISOLATION_UNPROVEN);
        }
        if (!req.isolation_domain.is_null() && !dev->isolation_domain.is_null() && req.isolation_domain != dev->isolation_domain)
            rep.rejected.push_back(RejectionReason::ISOLATION_DOMAIN_MISMATCH);
        if (!req.tenant.is_null() && !dev->tenant.is_null() && req.tenant != dev->tenant)
            rep.rejected.push_back(RejectionReason::TENANT_MISMATCH);

        if (req.required_port && !has_port(*dev, *req.required_port)) rep.rejected.push_back(RejectionReason::PORT_MISMATCH);
        if (req.required_function && !has_function(*dev, *req.required_function)) rep.rejected.push_back(RejectionReason::FUNCTION_MISMATCH);

        double hr = headroom_pct(*dev, u);
        double qp = queue_pressure(*dev, u);
        if (hr < static_cast<double>(policy.min_headroom)) rep.rejected.push_back(RejectionReason::HEADROOM_LOW);
        if (qp * 100.0 > static_cast<double>(policy.max_queue_pressure)) rep.rejected.push_back(RejectionReason::QUEUE_PRESSURE_HIGH);

        if (req.security == SecurityMode::SECURE || req.security == SecurityMode::ISOLATED) {
            if (!(iso_proven)) rep.rejected.push_back(RejectionReason::SECURITY_MODE_INCOMPATIBLE);
        }

        if (req.min_locality != LocalityQuality::UNKNOWN) {
            if (locality_value(dev->locality) < locality_value(req.min_locality))
                rep.rejected.push_back(RejectionReason::LOCALITY_INSUFFICIENT);
        }

        if (svc) {
            for (auto dep : svc->dependencies) {
                if (!snap.service(dep)) { rep.rejected.push_back(RejectionReason::DEPENDENCY_UNAVAILABLE); break; }
            }
        }

        ResourceRequest rr = svc ? svc->resource : req.resource;
        if (!can_admit(dev->resource_capacity, u, rr))
            rep.rejected.push_back(RejectionReason::RESOURCE_INSUFFICIENT);

        rep.passed_hard = rep.rejected.empty();
        if (rep.passed_hard) {
            bool warm = false;
            for (auto& [depl_id, depl] : snap.deployments) {
                if (depl.device == did && (!svc || depl.service == svc->service_id) && depl.state == DeploymentState::ACTIVE)
                    warm = true;
            }
            auto add = [&](RankFactor f, double norm, std::string note) {
                FactorScore s;
                s.factor = f;
                s.normalized = norm;
                s.weighted = norm * static_cast<double>(policy.weight_of(f));
                s.note = std::move(note);
                rep.scores.push_back(s);
            };
            add(RankFactor::WARM_SERVICE, warm ? 1.0 : 0.0, warm ? "warm=YES" : "warm=NO");
            add(RankFactor::LOCALITY, locality_value(dev->locality), std::string(locality_quality_name(dev->locality)));
            add(RankFactor::HEADROOM, hr / 100.0, std::to_string((int)hr));
            add(RankFactor::QUEUE_PRESSURE, 1.0 - qp, std::to_string((int)(qp * 100)));
            add(RankFactor::DEVICE_LOAD, 1.0 - device_load(*dev, u), std::to_string((int)(device_load(*dev, u) * 100)));
            add(RankFactor::STATE_TRANSFER, warm ? 1.0 : 0.4, warm ? "low" : "high");
            double host_save = (req.offload_class == OffloadClass::ENCRYPTION || req.offload_class == OffloadClass::COMPRESSION) ? 0.8 : 0.5;
            add(RankFactor::HOST_CPU_SAVINGS, host_save, "estimated");
            add(RankFactor::OFFLOAD_VALUE, warm ? 0.7 : 0.5, "estimated");
            add(RankFactor::FAILURE_RISK, static_cast<double>(health_severity(dev->health)) / 5.0, "estimated");
            add(RankFactor::ENERGY, 0.5, "unset");
            bool pref = false;
            for (auto& pdev : policy.preferred_devices) if (pdev == did) pref = true;
            add(RankFactor::POLICY_PREFERENCE, pref ? 1.0 : 0.0, pref ? "YES" : "NO");
            add(RankFactor::TIEBREAK_ID, 0.0, "identity");

            double total = 0.0;
            for (auto& s : rep.scores) total += s.weighted;
            rep.total = total;
            passed.push_back(rep);
        }
        all.push_back(std::move(rep));
    }

    result.candidates = all;

    if (!passed.empty()) {
        std::stable_sort(passed.begin(), passed.end(), [&](const CandidateReport& a, const CandidateReport& b) {
            if (a.total != b.total) return a.total > b.total;
            return device_order_index(snap, a.device) < device_order_index(snap, b.device);
        });
        result.hard_pass = true;
        result.selected = passed.front().device;
        const DeviceRecord* dev = snap.device(*result.selected);
        if (dev) {
            result.execution = (dev->device_class == DeviceClass::DPU) ? ExecutionClass::DPU_OFFLOAD :
                               (dev->device_class == DeviceClass::SMARTNIC) ? ExecutionClass::SMARTNIC_OFFLOAD :
                               (dev->device_class == DeviceClass::INFRA_PROCESSOR) ? ExecutionClass::INFRA_PROCESSOR_OFFLOAD :
                               ExecutionClass::HOST_FALLBACK;
        }
        result.fallback_used = false;
        result.explanation = build_explanation(result, snap);
    } else {
        result.hard_pass = false;
        result.selected.reset();
        if (req.mode == OffloadMode::OFFLOAD_REQUIRED) {
            result.execution = ExecutionClass::UNSUPPORTED;
            result.global_rejections.push_back(RejectionReason::OFFLOAD_REQUIRED_UNAVAILABLE);
            result.explanation = "OFFLOAD_REQUIRED: no authoritative offload candidate; host fallback not permitted";
        } else if (req.allow_host_fallback) {
            result.execution = ExecutionClass::HOST_FALLBACK;
            result.fallback_used = true;
            result.fallback_reason = "DPU_CAPABILITY_UNAVAILABLE";
            result.explanation = "OFFLOAD_PREFERRED: insufficient authoritative DPU candidates; explicit host fallback selected";
        } else {
            result.execution = ExecutionClass::UNSUPPORTED;
            result.explanation = "OFFLOAD_PREFERRED: no authoritative offload candidate and fallback not permitted";
        }
    }

    return result;
}

} // namespace dpufabric
