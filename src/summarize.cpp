
#include "dpufabric/eligibility.h"
#include <string>

namespace dpufabric {

std::string EligibilityResult::summarize() const {
    std::string s;
    s += "mode=";
    s += (mode == OffloadMode::OFFLOAD_REQUIRED) ? "OFFLOAD_REQUIRED" : "OFFLOAD_PREFERRED";
    s += " execution=";
    s += execution_class_name(execution);
    if (selected) s += " selected=" + selected->encode();
    s += " hard_pass=";
    s += hard_pass ? "true" : "false";
    if (fallback_used) s += " fallback=" + fallback_reason;
    if (!explanation.empty()) { s += " | "; s += explanation; }
    return s;
}

} // namespace dpufabric
