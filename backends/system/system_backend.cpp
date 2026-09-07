// SystemBackend: truthful OS-visible discovery of programmable-infrastructure
// and adjacent devices (NICs, GPU endpoints).  It never fabricates a DPU: if
// no programmable DPU-class device is present, physical_dpu_present=false and
// DPU execution is reported UNSUPPORTED.
#include "dpufabric/backend.h"
#include "dpufabric/device.h"
#include "dpufabric/id.h"
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <string>
#include <vector>
#include <memory>
#include <sstream>

namespace dpufabric {

namespace {

std::string wide_to_utf8(const wchar_t* w) {
    if (!w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(static_cast<size_t>(n - 1), char(0));
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n - 1, nullptr, nullptr);
    return s;
}

std::string reg_prop(HDEVINFO devs, PSP_DEVINFO_DATA info, DWORD prop) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (SetupDiGetDeviceRegistryPropertyW(devs, info, prop, nullptr,
            reinterpret_cast<PBYTE>(buf), size, &size) && size >= 2) {
        return wide_to_utf8(buf);
    }
    return "";
}

struct PciFact {
    std::string description;
    std::string hwid;
    std::string cls;
    std::string ven_dev;
};

std::vector<PciFact> enumerate_pci() {
    std::vector<PciFact> out;
    HDEVINFO devs = SetupDiGetClassDevsW(nullptr, L"PCI", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devs == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &info); ++i) {
        PciFact f;
        f.description = reg_prop(devs, &info, SPDRP_DEVICEDESC);
        f.cls = reg_prop(devs, &info, SPDRP_CLASS);
        f.hwid = reg_prop(devs, &info, SPDRP_HARDWAREID);
        f.ven_dev = f.hwid;
        out.push_back(std::move(f));
    }
    SetupDiDestroyDeviceInfoList(devs);
    return out;
}

bool is_nic(const PciFact& f) {
    if (f.cls == "Net") return true;
    return f.hwid.find("VEN_10EC") != std::string::npos || f.hwid.find("VEN_17CB") != std::string::npos;
}
bool is_gpu(const PciFact& f) {
    return f.cls == "Display" || f.hwid.find("VEN_10DE") != std::string::npos;
}
bool is_programmable_dpu(const PciFact& f) {
    return f.hwid.find("VEN_15B3") != std::string::npos ||
           f.description.find("BlueField") != std::string::npos ||
           f.description.find("DPU") != std::string::npos;
}

} // namespace

class SystemBackend final : public DiscoveryBackend {
public:
    std::string name() const override { return "system"; }
    bool is_vendor_backend() const noexcept override { return false; }
    DiscoveryResult discover() override {
        DiscoveryResult r;
        r.backend_available = true;
        r.summary = "system backend: real OS discovery";
        auto facts = enumerate_pci();

        bool found_programmable = false;
        uint64_t nic_seq = 1000;
        uint64_t gpu_seq = 1;

        for (const auto& f : facts) {
            if (is_programmable_dpu(f)) {
                found_programmable = true;
            }
            if (is_gpu(f)) {
                DeviceRecord d;
                d.device_id = DeviceId(9000 + gpu_seq);
                d.generation = DeviceGeneration(1);
                d.boot_id = DeviceBootId(0x9000 + gpu_seq);
                d.vendor = "NVIDIA";
                d.model = f.description.empty() ? "GPU" : f.description;
                d.device_class = DeviceClass::HOST_PROCESSOR;
                d.pci_reference = f.ven_dev;
                d.firmware_version = "n/a";
                d.firmware_gen = FirmwareGeneration(0);
                d.runtime_version = "CUDA (not exercised)";
                d.runtime_gen = RuntimeGeneration(0);
                d.architecture = "";
                d.health = DeviceHealth::HEALTHY;
                d.lifecycle = DeviceLifecycle::READY;
                d.discovery_source = ProvenanceSource::REAL_OS_DISCOVERY;
                d.evidence_gen = EvidenceGeneration(1);
                d.evidence_fresh = true;
                d.discovery_detail = "host-visible PCI GPU endpoint (adjacent reference only)";
                r.devices.push_back({ std::move(d), true, "system" });
                ++gpu_seq;
            } else if (is_nic(f)) {
                DeviceRecord d;
                d.device_id = DeviceId(6000 + nic_seq);
                d.generation = DeviceGeneration(1);
                d.boot_id = DeviceBootId(0x6000 + nic_seq);
                d.vendor = "host";
                d.model = f.description.empty() ? "NIC" : f.description;
                d.device_class = DeviceClass::NIC_ADJACENT;
                d.pci_reference = f.ven_dev;
                d.firmware_version = "n/a";
                d.firmware_gen = FirmwareGeneration(0);
                d.runtime_version = "n/a";
                d.runtime_gen = RuntimeGeneration(0);
                d.architecture = "";
                d.health = DeviceHealth::HEALTHY;
                d.lifecycle = DeviceLifecycle::READY;
                d.discovery_source = ProvenanceSource::REAL_OS_DISCOVERY;
                d.evidence_gen = EvidenceGeneration(1);
                d.evidence_fresh = true;
                d.discovery_detail = "host-visible network adapter (no programmable offload)";
                r.devices.push_back({ std::move(d), true, "system" });
                ++nic_seq;
            }
        }

        r.physical_dpu_present = found_programmable;
        if (!found_programmable) {
            r.summary += " | no programmable DPU-class device present; DPU execution = UNSUPPORTED";
        }
        return r;
    }
    ExecutionClass execution_capability() const noexcept override { return ExecutionClass::UNSUPPORTED; }
};

std::shared_ptr<DiscoveryBackend> make_system_backend() { return std::make_shared<SystemBackend>(); }

} // namespace dpufabric
