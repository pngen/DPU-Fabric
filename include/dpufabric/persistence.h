/// DPU Fabric: durable persistence with integrity and conservative recovery.
#pragma once
#include <cstdint>
#include <string>
#include "dpufabric/id.h"
#include "dpufabric/registry.h"

namespace dpufabric {

// The on-disk state file.  Versioned, integrity checked, bounded, strictly
// parsed, atomically replaced (temp + flush/close + rename on Windows).
// The format carries only durable knowledge; live authority is NOT persisted
// as fresh.  Dynamic evidence requiring physical confirmation is conservatively
// fenced into REVALIDATION_REQUIRED during recovery.
class PersistenceStore {
public:
    static constexpr uint32_t kMagic = 0x44504652;   // "DPFR" little-endian
    static constexpr uint16_t kFormatVersion = 1;
    static constexpr size_t kMaxFileBytes = 1u << 30;  // 1 GiB hard bound

    explicit PersistenceStore(std::string path) : path_(std::move(path)) {}
    const std::string& path() const noexcept { return path_; }

    // Serialize a snapshot to the file (atomic replace).  Throws on error.
    void save(const Snapshot& s);

    // Load and reconstruct live state.  On success, the passed LiveState is
    // filled with durable structural knowledge; every dynamic-authority field
    // is conservatively fenced so that nothing is trusted as fresh.  The
    // coordinator epoch in `out` is set to the `fresh_epoch` provided by the
    // caller, never the persisted one.
    //
    // Returns false when the file does not exist (fresh coordinator); throws on
    // corrupt/truncated/trailing-garbage state.
    bool load(LiveState& out, CoordinatorEpoch fresh_epoch);

private:
    std::string path_;
};

} // namespace dpufabric
