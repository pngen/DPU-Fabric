# DPU Fabric

DPU Fabric is a production-quality, **vendor-neutral runtime** for governing programmable infrastructure offload across DPUs, SmartNICs, infrastructure processors, host processors, NIC-adjacent compute, and other programmable data-path devices.

## The systems question DPU Fabric answers

> Which infrastructure function may be offloaded to which programmable infrastructure device **now** - under what capabilities, isolation, locality, compatibility, resource, lifecycle, and generation-bound authority - and what must happen when that offload is no longer safe or available?

DPU Fabric is **not** a demo, a wrapper around a vendor SDK, a packet processor, a fake DPU, or a general compute scheduler. It is a reusable **systems / runtime boundary** that serious infrastructure software builds against.

## Why a runtime boundary is necessary

A programmable device being *visible* does not make an offload *valid*. A program being *loaded* does not make it *active*. A prior activation surviving in durable metadata does not make it *authoritative* after a restart. Host execution is **not** DPU execution. Synthetic DPU semantics are **not** physical DPU proof.

| Fact | Not enough for |
|---|---|
| Device present | Offload validity |
| Capability name present | Authoritative capability |
| Program loaded | Active service |
| Deployment succeeded once | Authority now |
| Activation persisted | Authority after restart |
| Host thread emulates DPU | Real DPU execution |

## Exact ownership boundary

DPU Fabric **owns**: programmable-infrastructure device identity, generations and boot/incarnation identity, capability representation, offload-function identity, service versions/generations, program/artifact identity and compatibility, deployment identity/attempts, activation identity and authority, execution and resource contexts, queue/port/function attachments, tenant and isolation-domain references, capability/evidence freshness, firmware/runtime/ABI compatibility evidence, locality and attachment evidence supplied by backends, offload eligibility, hard capability matching, isolation validation, admission, deployment planning and lifecycle, activation lifecycle, drain/replacement, failure and degraded operation, revalidation and recovery, stale-authority rejection, deterministic candidate ranking, explicit fallback, persistence of durable knowledge, conservative recovery of dynamic evidence, structured provenance, generation-bound execution/activation authority, and DPU Fabric-owned resource accounting.

DPU Fabric does **not** own: generic NIC residency or PCI locality (NIC Residency), GPUDirect eligibility/execution (GPU Direct Fabric), RDMA buffer lifecycle/memory-registration keys/protection domains (RDMA Buffer), generic network transport or TCP/IP, RDMA verbs/QP, routing, NVLink/NVSwitch, PCIe topology, NUMA, GPU memory allocation, host memory management, storage-object semantics, collective communication, general placement, global resource arbitration, global workload or fabric scheduling, bandwidth or congestion governance, process scheduling, Kubernetes/Slurm integration, DPU firmware flashing, DPU OS installation, vendor fleet management, or arbitrary user applications on a DPU. Neighboring facts are represented through explicit interfaces, references, evidence, or adapters, never by absorbing adjacent repositories.

## Core architecture

- **Strong typed identities** (`DeviceId`, `DeviceGeneration`, `DeviceBootId`, `WorkerBootId`, `CoordinatorEpoch`, `ServiceId`, `ProgramId`, `ArtifactId`, `DeploymentId`, `ActivationId`, `ExecutionContextId`, `ResourceContextId`, `PortId`, `FunctionId`, `QueueId`, `TenantId`, `IsolationDomainId`, plus generation/boot/epoch tags). Generations and boot/epoch tokens reject zero; cross-type substitution is impossible at compile time.
- **Device model**: a vendor-neutral record with structural identity, firmware and runtime generations, architecture, resources, port/function attachments, isolation and locality, health, lifecycle, capability evidence, and provenance. Raw vendor strings are preserved separately for provenance only.
- **Device lifecycle** state machine (`DISCOVERED -> AVAILABLE -> PROVISIONING -> READY -> DEGRADED / DRAINING / REVALIDATION_REQUIRED / FAILED / OFFLINE / RETIRED`). Transitions are validated; readiness is never inferred from discovery.
- **Capability model**: a capability carries a synchronous state (`SUPPORTED`/`UNSUPPORTED`/`UNKNOWN`/`REVALIDATION_REQUIRED`), version, firmware/runtime floors, scale bounds, isolation requirements, evidence source and freshness. `UNKNOWN` and `REVALIDATION_REQUIRED` **fail closed** for hard requirements.
- **Service / program / deployment / activation model**: a service definition describes what is required; a program artifact is a deployable/targeted artifact; a deployment is an installation on a device; an activation is the generation-bound authority granting it to be active.
- **Eligibility engine**: applies hard constraints first (device exists, READY, evidence current, capability SUPPORTED and fresh, firmware/runtime compatible, artifact/ABI/architecture compatible, isolation compatible, tenant allowed, port/function/attachment compatible, resource headroom sufficient, queue pressure, dependencies, security mode, locality, policy), then ranks deterministically with named factors (warm service, locality, headroom, queue pressure, device load, state-transfer cost, host CPU savings, offload value, failure risk, energy, policy preference, stable identity tie-break) and returns structured, reproducible explanations - never an opaque score.
- **Activation authority**: an activation is authoritative only while every generation/boot/epoch component matches the current coordinator epoch, worker boot, device boot, device/service/program/deployment/activation generation, policy generation, and evidence generation.
- **Deployment lifecycle**: transactional `PLAN -> RESERVE -> STAGE -> LOAD -> VALIDATE -> ACTIVATE -> COMMIT`, with `ABORT / ROLLBACK / RELEASE` on failure. A failed attempt never poisons the logical service; retries carry a new attempt identity; stale completions are rejected.
- **Isolation**: first-class. Dedicated device/function/queue/context, shared-but-isolated, tenant-compatible, security/trust domain. Unproven hardware isolation is treated as `UNKNOWN`/`UNSUPPORTED`, never assumed.
- **Fallback**: explicit. `DPU_OFFLOAD / SMARTNIC_OFFLOAD / INFRA_PROCESSOR_OFFLOAD / HOST_FALLBACK / UNSUPPORTED`. `OFFLOAD_REQUIRED` rejects on no authoritative candidate; `OFFLOAD_PREFERRED` may return an explicit host fallback with a stated reason.
- **Resource governance**: DPU Fabric-owned logical accounting (slots, contexts, program slots, queue consumption, programmable memory) is transactional; no leakage after failure, cancellation, rollback, worker death, coordinator restart, drain, or retirement. Resource accounting never goes negative and never exceeds capacity.
- **Persistence**: versioned, integrity checked (CRC-32 for integrity, not authentication), bounded, strictly parsed, atomically replaced (temp + flush/close + rename). Durable structural knowledge recovers; live authority is **never** restored as fresh - dynamic evidence that requires physical confirmation becomes `REVALIDATION_REQUIRED`.
- **Concurrency**: mutation is serialized under a single narrow mutex; readers consume immutable snapshots via `std::atomic<std::shared_ptr<const Snapshot>>` with no lock held. Policy evaluation, backend calls, and persistence I/O never happen under the mutation lock.

## Backends

| Backend | Source | Physical DPU |
|---|---|---|
| `SystemBackend` | `REAL_OS_DISCOVERY` | reports presence truthfully |
| `SyntheticBackend` | `SYNTHETIC` | modeled DPUs, not physical |
| `UnsupportedBackend` | `UNSUPPORTED` | explicit unavailable |

A vendor backend is **not** bundled; the core never depends on a vendor SDK.

## Evidence classes: REAL, SYNTHETIC, UNSUPPORTED

Every hardware/runtime fact is classified: **REAL** (OS or vendor-API discovery), **SYNTHETIC** (deterministic modeled semantics), **UNSUPPORTED** (not available). These are never blurred. Synthetic evidence is never reported as REAL; a host process is never reported as DPU execution; library presence is never conflated with device support.

## Hardware validation on the development machine

Real OS discovery on this machine found: an NVIDIA GeForce RTX 5090 (PCI `VEN_10DE&DEV_2B85`), an AMD Radeon graphics adapter, a Realtek PCIe 5GbE NIC, a Qualcomm FastConnect 7800 Wi-Fi adapter, and audio controllers. **No DPU / BlueField / SmartNIC / infrastructure-processor programmable device is present.**

- Backend: system (`REAL`), synthetic (2 modeled DPUs), unsupported.
- `physical_dpu_present = false`.
- **Physical DPU execution: UNSUPPORTED on this machine.**

This is a *correct* result, not an apology. It is part of the engineering to classify unsupported hardware precisely rather than fabricate proof. All DPU Fabric offload semantics demonstrated here are `SYNTHETIC`; the multiprocess control-plane proofs are `REAL` OS-process proofs of the runtime own fencing/recovery behavior.

## Build

Requirements: Windows 11 x64, MSVC 2022 (C++20), CMake 3.20+, Ninja.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Debug equivalents use `-DCMAKE_BUILD_TYPE=Debug`. The build is configured with `/W4 /WX` on MSVC (warnings-as-errors) and produced **zero warnings** in both Release and Debug.

## Install

```
cmake --install build --config Release --prefix <prefix>
```

Installs headers under `<prefix>/include/dpufabric`, the static libraries under `<prefix>/lib`, and a CMake package config under `<prefix>/lib/cmake/DPUFabric`.

## find_package usage

```cmake
find_package(DPUFabric CONFIG REQUIRED)
target_link_libraries(app PRIVATE DPUFabric::core DPUFabric::synthetic)
```

Exported targets: `DPUFabric::core`, `DPUFabric::synthetic`, `DPUFabric::system`, `DPUFabric::unsupported`, `DPUFabric::mp`. No source-tree knowledge is required by consumers.

## Examples

- `basic_device_registry` - register the two synthetic DPUs.
- `capability_query` - query capabilities on a device.
- `offload_selection` - deterministic offload selection with explanation.
- `explicit_fallback` - `OFFLOAD_PREFERRED` host fallback with a stated reason.
- `deployment_lifecycle` - a full deploy to ACTIVE, authoritative.
- `stale_authority_rejection` - a device reboot fences prior authority.
- `synthetic_multi_dpu` - per-class device selection.
- `persistence_recovery` - recover durable structure with evidence fenced.

## CLI

```
dpu-fabric discover
 dpu-fabric devices
 dpu-fabric capabilities
 dpu-fabric explain --service encryption
 dpu-fabric plan
 dpu-fabric services / deployments / snapshot
 dpu-fabric validate-state
 dpu-fabric synthetic-demo
```

`discover` reports the system backend (REAL: NVIDIA RTX 5090, Realtek 5GbE, Qualcomm Wi-Fi, ...), the synthetic backend (2 modeled DPUs), and the unsupported backend, and states **"Physical DPU execution: UNSUPPORTED on this machine."**

## Multiprocess control plane

Real OS processes `dpu_fabric_coordinator` and `dpu_fabric_worker` communicate over a framed, versioned, checksummed TCP transport with strict bounds. The automated proofs:

- **Worker death / restart**: register device + evidence, establish an authoritative deployment/activation, kill the worker OS process, observe the coordinator fence the affected device (`REVALIDATION_REQUIRED`, new deployment rejected), restart the worker with a fresh boot id, establish fresh authority, and reject the old boot id.
- **Coordinator death / restart**: persist durable state, kill the coordinator OS process, start a fresh coordinator, verify the epoch advances (N to N+1), durable structure recovers, dynamic evidence is **not** fresh until revalidation, and fresh authority is established under the new epoch.

## Limitations

- No physical programmable infrastructure device was available; DPU execution is `UNSUPPORTED` on this machine. All offload *semantics* are proven with synthetic evidence; the multiprocess proofs are real OS-process proofs.
- No vendor SDK (e.g. DOCA/BlueField) is linked or validated.
- CRC-32 is integrity, not authentication; artifact hashing is SHA-256 identity, not signing.
- Isolation is modeled and enforced; unproven hardware isolation is treated as `UNKNOWN`/`UNSUPPORTED`. Synthetic tests prove semantics, not hardware security.

## Tests

All tests pass in Release and Debug with zero warnings. Areas covered: strong IDs, serialization, enums, error round-trip, state machines, generation validation, registry (register/update/duplicate/conflict/retire/snapshot/deterministic ordering), capabilities (supported/unsupported/unknown/stale/version/firmware/runtime mismatch), policy (hard filters, ranking, stable ties, fallback, `OFFLOAD_REQUIRED`, `OFFLOAD_PREFERRED`), service/program identity and compatibility, deployment lifecycle (plan/reserve/stage/load/validate/activate/commit/cancel/rollback/drain/retire/failure), authority (stale epoch/worker/device boot/device generation/service generation/program generation/deployment generation/activation generation), isolation (tenant/domain/dedicated/shared/unknown-fails-closed), resources (exact reservation/exhaustion/rollback/no leak), persistence (round-trip, corrupt checksum, truncation, bad magic, bad version, oversized count, trailing garbage), protocol (valid/zero-length/malformed/truncation/oversize/checksum corruption/unknown type), concurrency stress, seeded property invariants, adversarial malformed input, the multiprocess worker-death and coordinator-restart proofs, and synthetic scenarios A-R.

The AddressSanitizer (CPU/core) subset passes with zero sanitizer errors.

## Benchmark (Release)

Measured on this machine with the Release build (offload evaluation and snapshot machinery - **not** packet or offload throughput): snapshot lookup/metadata and offload evaluation across 100k iterations reach hundreds of thousands to millions of operations/second. See `benchmarks/bench_core.cpp`.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
