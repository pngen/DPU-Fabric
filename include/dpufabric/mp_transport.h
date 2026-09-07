/// DPU Fabric: framed TCP transport for the multiprocess control plane.
#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <optional>
#include "dpufabric/protocol.h"
#include "dpufabric/error.h"

// The OS socket handle.  Windows SOCKET is an unsigned pointer-sized type and
// POSIX uses a signed int; a uintptr_t accommodates both without pulling in
// platform headers here.
namespace dpufabric {
namespace mp {
typedef uintptr_t DSH_SOCKET_T;

// Read exactly n bytes from a socket with a timeout.  Returns false on EOF/error/timeout.
bool recv_n(DSH_SOCKET_T sock, uint8_t* buf, size_t n, int timeout_ms);
// Write n bytes; returns false on error.
bool send_n(DSH_SOCKET_T sock, const uint8_t* buf, size_t n);

// Distinguish the outcome of a socket read so a coordinator can tell an idle
// connection from a closed one (worker death).
enum class RecvResult : uint8_t { OK, TIMEOUT, CLOSED, ERR };

// Read exactly one frame from a socket.  Returns OK on a full valid frame and
// TIMEOUT when no data is available within timeout_ms.  CLOSED means the peer
// closed/reset the connection.  Throws a typed error on a malformed / over-size
// / checksum-mismatched frame.  timeout_ms < 0 blocks indefinitely.
RecvResult recv_frame(DSH_SOCKET_T sock, proto::Frame& out, int timeout_ms);

// Layered on recv_frame for simple clients: nullopt on non-OK.
bool read_frame(DSH_SOCKET_T sock, proto::Frame& out, int timeout_ms);
// Write one frame as bytes.
bool write_frame(DSH_SOCKET_T sock, proto::MsgType type, std::span<const uint8_t> payload);

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient();
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    bool connect(const std::string& host, uint16_t port, int timeout_ms = 5000);
    void send(proto::MsgType type, std::span<const uint8_t> payload);
    std::optional<proto::Frame> recv(int timeout_ms);   // nullopt on timeout/EOF
    void close_() noexcept;
    bool connected() const noexcept;
private:
    DSH_SOCKET_T sock_ = static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0));
};

class TcpServer {
public:
    explicit TcpServer(uint16_t port);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    bool start();                            // bind + listen
    std::optional<DSH_SOCKET_T> accept(int timeout_ms);
    void close_socket(DSH_SOCKET_T s) noexcept;
    void stop() noexcept;
    uint16_t port() const noexcept { return port_; }
private:
    DSH_SOCKET_T listen_sock_ = static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0));
    uint16_t port_;
};

// Initialize Winsock once.  Idempotent.
bool init_transport();
void shutdown_transport();

} // namespace mp
} // namespace dpufabric
