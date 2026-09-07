#include "dpufabric/mp_transport.h"
// Winsock must be included before windows.h; here we require winsock2 only.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#endif
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

namespace dpufabric {
namespace mp {

namespace {
std::atomic<bool> g_inited{ false };

bool to_sock_noerr() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void close_raw(DSH_SOCKET_T s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

bool wait_readable(DSH_SOCKET_T s, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(static_cast<int>(s) + 1, &fds, nullptr, nullptr, &tv);
    return r > 0 && FD_ISSET(s, &fds);
}
} // namespace

bool init_transport() {
    if (g_inited.exchange(true)) return true;
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void shutdown_transport() {
#ifdef _WIN32
    WSACleanup();
#endif
    g_inited = false;
}

bool recv_n(DSH_SOCKET_T sock, uint8_t* buf, size_t n, int timeout_ms) {
    size_t got = 0;
    while (got < n) {
        if (timeout_ms >= 0) {
            if (!wait_readable(sock, timeout_ms)) return false;
        }
        int r = static_cast<int>(recv(sock, reinterpret_cast<char*>(buf + got), static_cast<int>(n - got), 0));
        if (r <= 0) return false;   // EOF or error
        got += static_cast<size_t>(r);
    }
    return true;
}

bool send_n(DSH_SOCKET_T sock, const uint8_t* buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        int r = static_cast<int>(send(sock, reinterpret_cast<const char*>(buf + sent), static_cast<int>(n - sent), 0));
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

RecvResult recv_frame(DSH_SOCKET_T sock, proto::Frame& out, int timeout_ms) {
    std::vector<uint8_t> hdr(proto::kHeaderSize);
    // One probe read: distinguishes idle (0) from closed (recv returns 0).
    if (!wait_readable(sock, timeout_ms)) return RecvResult::TIMEOUT;
    int r = static_cast<int>(recv(sock, reinterpret_cast<char*>(hdr.data()), proto::kHeaderSize, 0));
    if (r == 0) return RecvResult::CLOSED;
    if (r < 0) return RecvResult::ERR;
    if (r != static_cast<int>(proto::kHeaderSize)) {
        // Partial header: read the remainder (blocking; socket is readable).
        if (!recv_n(sock, hdr.data() + r, proto::kHeaderSize - static_cast<size_t>(r), -1)) return RecvResult::CLOSED;
    }
    uint32_t plen;
    {
        wire::Reader reader{std::span<const uint8_t>(hdr)};
        (void)reader.u32();   // magic
        (void)reader.u16();   // version
        (void)reader.u16();   // type
        plen = reader.u32();  // payload_len
        (void)reader.u32();   // crc
    }
    if (plen > proto::kMaxPayload) throw_error(ErrorCode::FRAME_TOO_LARGE, "frame payload over bound");
    std::vector<uint8_t> payload(plen);
    if (plen > 0) {
        if (!recv_n(sock, payload.data(), plen, -1)) return RecvResult::CLOSED;
    }
    std::vector<uint8_t> full;
    full.reserve(proto::kHeaderSize + plen);
    full.insert(full.end(), hdr.begin(), hdr.end());
    full.insert(full.end(), payload.begin(), payload.end());
    out = proto::decode(full);
    return RecvResult::OK;
}

bool read_frame(DSH_SOCKET_T sock, proto::Frame& out, int timeout_ms) {
    return recv_frame(sock, out, timeout_ms) == RecvResult::OK;
}

bool write_frame(DSH_SOCKET_T sock, proto::MsgType type, std::span<const uint8_t> payload) {
    auto bytes = proto::encode(type, payload);
    return send_n(sock, bytes.data(), bytes.size());
}

// -------- TcpClient --------
TcpClient::~TcpClient() { close_(); }

bool TcpClient::connect(const std::string& host, uint16_t port, int timeout_ms) {
    (void)timeout_ms;
    init_transport();
    DSH_SOCKET_T s;
#ifdef _WIN32
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
#else
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) { close_raw(s); return false; }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_raw(s); return false; }
    sock_ = s;
    return true;
}

void TcpClient::send(proto::MsgType type, std::span<const uint8_t> payload) {
    if (!write_frame(sock_, type, payload)) throw_error(ErrorCode::IO_ERROR, "send failed");
}

std::optional<proto::Frame> TcpClient::recv(int timeout_ms) {
    proto::Frame f;
    if (read_frame(sock_, f, timeout_ms)) return f;
    return std::nullopt;
}

void TcpClient::close_() noexcept {
    if (sock_ != static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0))) { close_raw(sock_); sock_ = static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0)); }
}

bool TcpClient::connected() const noexcept { return sock_ != static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0)); }

// -------- TcpServer --------
TcpServer::TcpServer(uint16_t port) : port_(port) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
    init_transport();
#ifdef _WIN32
    listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == INVALID_SOCKET) return false;
#else
    listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock_ < 0) return false;
#endif
    int one = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    if (bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(listen_sock_);
#else
        ::close(listen_sock_);
#endif
        listen_sock_ = static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0));
        return false;
    }
    if (listen(listen_sock_, 16) != 0) return false;
    return true;
}

std::optional<DSH_SOCKET_T> TcpServer::accept(int timeout_ms) {
    if (timeout_ms >= 0) {
        if (!wait_readable(listen_sock_, timeout_ms)) return std::nullopt;
    }
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    DSH_SOCKET_T s = ::accept(listen_sock_, reinterpret_cast<sockaddr*>(&addr), &len);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return std::nullopt;
#else
    if (s < 0) return std::nullopt;
#endif
    return s;
}

void TcpServer::close_socket(DSH_SOCKET_T s) noexcept { close_raw(s); }

void TcpServer::stop() noexcept {
    if (listen_sock_ != static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0))) {
        close_raw(listen_sock_);
        listen_sock_ = static_cast<DSH_SOCKET_T>(~static_cast<DSH_SOCKET_T>(0));
    }
}

} // namespace mp
} // namespace dpufabric
