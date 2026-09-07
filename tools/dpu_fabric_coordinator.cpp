// dpu_fabric_coordinator: a real OS-process coordinator for the multiprocess proof.
#include "dpufabric/mp_transport.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "mpproto.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <chrono>
#include <fstream>

using namespace dpufabric;
using namespace dpufabric::synth;

namespace {
std::string arg_value(int argc, char** argv, const char* name, const std::string& def) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) return std::string(argv[i + 1]);
    }
    return def;
}
uint64_t arg_u64(int argc, char** argv, const char* name, uint64_t def) {
    return static_cast<uint64_t>(std::strtoull(arg_value(argc, argv, name, std::to_string(def)).c_str(), nullptr, 10));
}
enum class Role : uint8_t { UNKNOWN, WORKER, ADMIN };
}

int main(int argc, char** argv) {
    uint16_t port = static_cast<uint16_t>(arg_u64(argc, argv, "--port", 0));
    uint64_t start_epoch = arg_u64(argc, argv, "--epoch", 1);
    std::string state_path = arg_value(argc, argv, "--state", "");

    DPUFabricEngine engine{CoordinatorEpoch(start_epoch)};
    bool recovered = false;
    if (!state_path.empty() && std::ifstream(state_path).good()) {
        recovered = engine.recover_from(state_path);
        std::printf("[coord] recovered=%d epoch=%s\n", (int)recovered, engine.epoch().encode().c_str());
    }
    std::printf("[coord] coordinator up epoch=%s port=%u\n", engine.epoch().encode().c_str(), (unsigned int)port);
    std::fflush(stdout);

    mp::TcpServer server(port);
    if (!server.start()) { std::fprintf(stderr, "[coord] cannot bind\n"); return 1; }
    std::printf("[coord] listening\n"); std::fflush(stdout);

    std::map<mp::DSH_SOCKET_T, Role> conns;
    std::map<mp::DSH_SOCKET_T, uint64_t> conn_worker;
    std::map<uint64_t, mp::DSH_SOCKET_T> sock_by_worker;
    bool running = true;
    uint64_t cmd_seq = 1;
    std::map<uint64_t, std::string> pending;

    auto persist_if = [&]() {
        if (!state_path.empty()) {
            try { engine.persist(state_path); std::printf("[coord] persisted\n"); std::fflush(stdout); }
            catch (const DpuError& e) { std::fprintf(stderr, "[coord] persist failed: %s\n", e.what()); }
        }
    };

    auto handle_worker_msg = [&](mp::DSH_SOCKET_T s, const proto::Frame& f, WorkerId wid) {
        (void)s;
        switch (f.type) {
            case proto::MsgType::REGISTER_DEVICE: {
                wire::Reader r(f.payload);
                DeviceRecord dev = mpproto::get_device(r);
                dev.owning_worker = wid;
                dev.owner_worker_boot = WorkerBootId(sock_by_worker.count(wid.as_u64()) ? wid.as_u64() + 1 : 0);
                try {
                    if (engine.snapshot()->device(dev.device_id)) engine.update_device(dev);
                    else engine.register_device(dev);
                    std::printf("[coord] registered device %s\n", dev.device_id.encode().c_str()); std::fflush(stdout);
                } catch (const DpuError& e) { std::fprintf(stderr, "[coord] device reg error: %s\n", e.what()); }
                break;
            }
            case proto::MsgType::PUBLISH_EVIDENCE: {
                wire::Reader r(f.payload);
                DeviceId did(r.u64());
                uint64_t eg = r.u64();
                bool fresh = r.u8() != 0;
                try {
                    auto rec = engine.snapshot()->device(did);
                    if (rec) {
                        DeviceRecord upd = *rec;
                        upd.evidence_gen = EvidenceGeneration(eg);
                        upd.evidence_fresh = fresh;
                        if (fresh) upd.lifecycle = DeviceLifecycle::READY;
                        engine.update_device(upd);
                        std::printf("[coord] evidence %s fresh=%d\n", did.encode().c_str(), (int)fresh);
                    }
                } catch (const DpuError&) {}
                break;
            }
            case proto::MsgType::RESULT: {
                wire::Reader r(f.payload);
                uint64_t cid = r.u64(); uint8_t ok = r.u8(); std::string detail = r.str();
                auto it = pending.find(cid);
                if (it != pending.end()) { std::printf("[coord] worker result %s ok=%d %s\n", std::to_string(cid).c_str(), (int)ok, detail.c_str()); pending.erase(it); }
                break;
            }
            default: break;
        }
    };

    auto handle_admin_msg = [&](mp::DSH_SOCKET_T s, const proto::Frame& f) {
        wire::Reader r(f.payload);
        uint64_t cmd_id = r.u64();
        uint8_t ctype = r.u8();
        auto reply = [&](bool ok, const std::string& text) {
            wire::Writer w; w.u64(cmd_id); w.u8(ok ? 1 : 0); w.str(text);
            std::vector<uint8_t> p(w.data().begin(), w.data().end());
            try { mp::write_frame(s, proto::MsgType::RESULT, p); } catch (...) {}
        };
        try {
            if (ctype == static_cast<uint8_t>(mpproto::Cmd::STATUS)) {
                reply(true, engine.describe());
            } else if (ctype == static_cast<uint8_t>(mpproto::Cmd::CREATE_DEPLOYMENT)) {
                ServiceId sid(r.u64());
                uint8_t oc = r.u8();
                OffloadRequest req;
                req.offload_class = static_cast<OffloadClass>(oc);
                req.service = sid;
                req.mode = OffloadMode::OFFLOAD_REQUIRED;
                auto elig = engine.evaluate(req);
                if (!elig.selected) {
                    reply(false, "REJECTED_NO_DEVICE");
                } else {
                auto dep = engine.reserve_deployment(req, elig);
                engine.stage_deployment(dep);
                engine.load_deployment(dep);
                engine.validate_deployment(dep);
                engine.activate_deployment(dep);
                engine.commit_deployment(dep);
                DeviceId dev = *elig.selected;
                std::string msg = "dep=" + dep.encode() + " device=" + dev.encode();
                reply(true, msg);
                auto wit = engine.snapshot()->device(dev);
                if (wit) {
                    auto sit = sock_by_worker.find(wit->owning_worker.as_u64());
                    if (sit != sock_by_worker.end()) {
                        wire::Writer cw; cw.u64(cmd_seq); cw.str("ACTIVATE");
                        std::vector<uint8_t> cp(cw.data().begin(), cw.data().end());
                        try { mp::write_frame(sit->second, proto::MsgType::COMMAND, cp); pending[cmd_seq] = "ACTIVATE"; ++cmd_seq; }
                        catch (...) {}
                    }
                }
                persist_if();
                }
            } else if (ctype == static_cast<uint8_t>(mpproto::Cmd::SHUTDOWN)) {
                persist_if();
                reply(true, "SHUTDOWN_OK");
                running = false;
            } else {
                reply(false, "UNKNOWN_CMD");
            }
        } catch (const DpuError& e) {
            reply(false, e.what());
        }
    };

    auto close_conn = [&](mp::DSH_SOCKET_T s) {
        auto wit = conn_worker.find(s);
        if (wit != conn_worker.end()) {
            engine.mark_worker_lost(WorkerId(wit->second));
            std::printf("[coord] worker %s lost; device evidence fenced\n", std::to_string(wit->second).c_str()); std::fflush(stdout);
            sock_by_worker.erase(wit->second);
        }
        conn_worker.erase(s);
        server.close_socket(s);
    };

    // Register the DPU-A service so CREATE_DEPLOYMENT can bind to it.
    ServiceDefinition svc;
    svc.service_id = ServiceId(90);
    svc.service_gen = ServiceGeneration(1);
    svc.offload_class = OffloadClass::SECURITY_SERVICE;
    CapabilityRequirement cr; cr.key = cap_encryption(); svc.required_caps.push_back(cr);
    CapabilityRequirement cn; cn.key = cap_network(); svc.required_caps.push_back(cn);
    svc.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    try { engine.register_service(svc); } catch (...) {}

    while (running) {
        auto lc = server.accept(0);
        if (lc) { conns[*lc] = Role::UNKNOWN; std::printf("[coord] accepted\n"); std::fflush(stdout); }

        for (auto it = conns.begin(); it != conns.end();) {
            mp::DSH_SOCKET_T s = it->first;
            Role role = it->second;
            proto::Frame f;
            mp::RecvResult rr = mp::recv_frame(s, f, 0);
            if (rr == mp::RecvResult::TIMEOUT) { ++it; continue; }
            if (rr == mp::RecvResult::CLOSED || rr == mp::RecvResult::ERR) {
                close_conn(s);
                it = conns.erase(it);
                continue;
            }
            // OK: process the frame.
            if (role == Role::UNKNOWN) {
                if (f.type == proto::MsgType::HELLO) {
                    wire::Reader r(f.payload);
                    uint64_t wk, boot; std::string label;
                    mpproto::decode_hello(r, wk, boot, label);
                    bool ok = true;
                    try { engine.register_worker(WorkerId(wk), WorkerBootId(boot)); }
                    catch (const DpuError&) { ok = false; }
                    if (!ok) {
                        std::printf("[coord] rejecting stale worker boot id (worker %s)\n", std::to_string(wk).c_str());
                        server.close_socket(s);
                        it = conns.erase(it);
                        continue;
                    }
                    it->second = Role::WORKER;
                    conn_worker[s] = wk;
                    wire::Writer w; w.u64(engine.epoch().as_u64()); w.u8(1);
                    std::vector<uint8_t> p(w.data().begin(), w.data().end());
                    mp::write_frame(s, proto::MsgType::ACK, p);
                    sock_by_worker[wk] = s;
                    std::printf("[coord] worker %s registered\n", std::to_string(wk).c_str()); std::fflush(stdout);
                } else {
                    it->second = Role::ADMIN;
                }
            }
            // Dispatch based on the (possibly just-derived) role; a frame that
            // established an ADMIN connection is handled immediately.
            if (it->second == Role::WORKER) {
                if (role == Role::WORKER) handle_worker_msg(s, f, WorkerId(conn_worker[s]));
            } else if (it->second == Role::ADMIN) {
                handle_admin_msg(s, f);
            }
            ++it;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    persist_if();
    server.stop();
    std::printf("[coord] coordinator exiting\n"); std::fflush(stdout);
    return 0;
}
