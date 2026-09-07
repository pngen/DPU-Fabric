// dpu_fabric_worker: a real OS-process worker for the multiprocess proof.
#include "dpufabric/mp_transport.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/device.h"
#include "dpufabric/id.h"
#include "mpproto.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
}

int main(int argc, char** argv) {
    std::string host = arg_value(argc, argv, "--host", "127.0.0.1");
    uint16_t port = static_cast<uint16_t>(arg_u64(argc, argv, "--port", 0));
    uint64_t worker = arg_u64(argc, argv, "--worker-id", 1);
    uint64_t boot = arg_u64(argc, argv, "--boot-id", 0);
    std::string label = arg_value(argc, argv, "--label", "worker");
    std::printf("[worker] starting worker id=%s boot=%s\n", std::to_string(worker).c_str(), std::to_string(boot).c_str());
    std::fflush(stdout);

    mp::TcpClient client;
    if (!client.connect(host, port, 5000)) { std::fprintf(stderr, "[worker] connect failed\n"); return 1; }

    auto hello_payload = mpproto::encode_hello(worker, boot, label);
    client.send(proto::MsgType::HELLO, hello_payload);
    auto ack = client.recv(5000);
    if (!ack) { std::fprintf(stderr, "[worker] no HELLO_ACK\n"); return 2; }
    if (ack->type != proto::MsgType::ACK) { std::fprintf(stderr, "[worker] expected ACK, got %s\n", proto::msg_type_name(ack->type)); return 2; }
    std::fprintf(stderr, "[worker] got ACK\n");

    DeviceRecord dev = make_dpu(DeviceId(1), "DPU-A",
        { cap_network(), cap_encryption(), cap_steering() },
        ResourceProfile{ 256u << 20, 8, 2, 16, 16 }, DeviceBootId(0xA1),
        { OffloadClass::NETWORK_SERVICE, OffloadClass::SECURITY_SERVICE, OffloadClass::PACKET_STEERING });
    dev.owning_worker = WorkerId(worker);
    dev.owner_worker_boot = WorkerBootId(boot);
    {
        wire::Writer w; mpproto::put_device(w, dev);
        std::vector<uint8_t> payload(w.data().begin(), w.data().end());
        client.send(proto::MsgType::REGISTER_DEVICE, payload);
    }

    {
        wire::Writer w; w.u64(dev.device_id.as_u64()); w.u64(dev.evidence_gen.as_u64()); w.u8(1);
        std::vector<uint8_t> payload(w.data().begin(), w.data().end());
        client.send(proto::MsgType::PUBLISH_EVIDENCE, payload);
    }

    bool running = true;
    while (running) {
        auto f = client.recv(5000);
        if (!f) {
            std::fprintf(stderr, "[worker] connection lost, exiting\n");
            break;
        }
        switch (f->type) {
            case proto::MsgType::COMMAND: {
                wire::Reader r(f->payload);
                uint64_t cmd_id; std::string action; mpproto::decode_command(r, cmd_id, action);
                wire::Writer w;
                w.u64(cmd_id); w.u8(1); w.str("worker-handled:" + action);
                std::vector<uint8_t> resp(w.data().begin(), w.data().end());
                client.send(proto::MsgType::RESULT, resp);
                std::fprintf(stderr, "[worker] handled command %s (%s)\n", std::to_string(cmd_id).c_str(), action.c_str());
                break;
            }
            case proto::MsgType::BYE: client.close_(); running = false; break;
            case proto::MsgType::PING: {
                wire::Writer w; w.u64(0);
                std::vector<uint8_t> p(w.data().begin(), w.data().end());
                client.send(proto::MsgType::PONG, p);
                break;
            }
            default: break;
        }
    }
    std::printf("[worker] exiting\n");
    std::fflush(stdout);
    return 0;
}
