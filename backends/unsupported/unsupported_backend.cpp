
#include "dpufabric/backend.h"

namespace dpufabric {

class UnsupportedBackend final : public DiscoveryBackend {
public:
    std::string name() const override { return "unsupported"; }
    bool is_vendor_backend() const noexcept override { return false; }
    DiscoveryResult discover() override {
        DiscoveryResult r;
        r.backend_available = true;
        r.physical_dpu_present = false;
        r.summary = "unsupported backend: no compatible programmable infrastructure hardware/runtime available";
        return r;
    }
    ExecutionClass execution_capability() const noexcept override { return ExecutionClass::UNSUPPORTED; }
};

std::shared_ptr<DiscoveryBackend> make_unsupported_backend() { return std::make_shared<UnsupportedBackend>(); }

} // namespace dpufabric
