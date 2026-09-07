/// DPU Fabric: isolation and multi-tenancy model.
#pragma once
#include <cstdint>
#include <string>
#include "dpufabric/id.h"
#include "dpufabric/types.h"

namespace dpufabric {

// An explicit isolation requirement expressed against a device's real runtime
// configuration.  Requirement levels are ordered from most strict to least.
struct IsolationSpec {
    IsolationRequirement level = IsolationRequirement::NONE;
    IsolationDomainId isolation_domain;    // null when not tenant-scoped
    TenantId tenant;                        // null when not tenant-scoped
    std::string trust_domain;               // e.g. "control", "tenant-a"
    std::string security_domain;            // e.g. "crypto", "dataplane"
    SecurityMode security = SecurityMode::UNVERIFIED;

    bool tenant_scoped() const noexcept { return !tenant.is_null(); }
    bool domain_scoped() const noexcept { return !isolation_domain.is_null(); }

    // A NONE requirement is trivially satisfied.
    bool trivially_satisfied() const noexcept { return level == IsolationRequirement::NONE; }

    // Whether the device's *authoritative* isolation configuration satisfies
    // this spec.  UNVERIFIED security never satisfies a SECURE/ISOLATED spec;
    // that is the fail-closed path for "capability exists" vs "isolation proven".
    bool satisfied_by(bool hardware_isolation_proven,
                      const IsolationDomainId& device_domain,
                      const TenantId& device_tenant) const noexcept {
        if (level == IsolationRequirement::NONE) return true;
        if (level == IsolationRequirement::SECURITY_DOMAIN) {
            if (security != SecurityMode::SECURE && security != SecurityMode::ISOLATED) {
                // security-domained requirement: must not be UNVERIFIED
                return false;
            }
            return hardware_isolation_proven;
        }
        if (level == IsolationRequirement::DEDICATED_DEVICE) {
            // dedicated device: matches when the device has no other tenant.
            return device_tenant.is_null() || (!tenant.is_null() && device_tenant == tenant);
        }
        // Dedicated function/queue/context, shared-isolated, tenant-compatible,
        // and trust-domain all require authority-proven hardware isolation
        // evidence plus domain/tenant agreement when scoped.
        if (!hardware_isolation_proven) return false;
        if (domain_scoped() && !device_domain.is_null() && device_domain != isolation_domain) return false;
        if (tenant_scoped() && !device_tenant.is_null() && device_tenant != tenant) return false;
        return true;
    }

    bool operator==(const IsolationSpec& o) const noexcept {
        return level == o.level && isolation_domain == o.isolation_domain && tenant == o.tenant &&
               trust_domain == o.trust_domain && security_domain == o.security_domain && security == o.security;
    }
};

} // namespace dpufabric
