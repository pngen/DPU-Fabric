#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#endif
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "dpufabric/mp_transport.h"

namespace mptest {

struct Proc {
#ifdef _WIN32
    HANDLE h = nullptr;
    DWORD pid = 0;
#else
    pid_t pid = -1;
#endif
    bool kill() {
#ifdef _WIN32
        if (h) { TerminateProcess(h, 1); WaitForSingleObject(h, 5000); return true; }
        return false;
#else
        if (pid > 0) { kill(pid, SIGKILL); return true; }
        return false;
#endif
    }
#ifdef _WIN32
    int wait_exit(int timeout_ms) {
        if (!h) return -1;
        if (WaitForSingleObject(h, timeout_ms) != WAIT_OBJECT_0) return -2;   // still running
        DWORD code = 0; GetExitCodeProcess(h, &code); CloseHandle(h); h = nullptr;
        return static_cast<int>(code);
    }
#else
    int wait_exit(int) { return -1; }
#endif
};

inline std::string dir_of(const std::string& p) {
    auto pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "" : p.substr(0, pos);
}

inline std::string tools_dir() {
    std::string self;
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        int w = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
        self.resize(w); WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, self.data(), w, nullptr, nullptr);
    }
#endif
    auto d = dir_of(self);
    auto cfg = d.substr(d.find_last_of("/\\") + 1);
    bool multi = (cfg == "Debug" || cfg == "Release" || cfg == "RelWithDebInfo" || cfg == "MinSizeRel");
    auto root = multi ? dir_of(dir_of(d)) : dir_of(d);
    return root + "/tools" + (multi ? ("/" + cfg) : std::string());
}

inline Proc spawn(const std::string& exe, const std::string& args) {
    Proc p;
#ifdef _WIN32
    std::string cmdline = "\"" + exe + "\" " + args;
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        p.h = pi.hProcess; p.pid = pi.dwProcessId;
    }
#else
    (void)exe; (void)args;
#endif
    return p;
}

inline uint16_t find_free_port() {
#ifdef _WIN32
    dpufabric::mp::init_transport();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = 0; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { closesocket(s); return 0; }
    int len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    uint16_t p = ntohs(a.sin_port);
    closesocket(s);
    return p;
#else
    return 0;
#endif
}

inline bool wait_port(uint16_t port, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < timeout_ms) {
        dpufabric::mp::TcpClient c;
        if (c.connect("127.0.0.1", port, 200)) { c.close_(); return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

inline std::string admin(uint16_t port, int ctype, const std::vector<uint8_t>& extra) {
    dpufabric::mp::TcpClient c;
    if (!c.connect("127.0.0.1", port, 3000)) return "<no-connect>";
    dpufabric::wire::Writer w;
    w.u64(1); w.u8(static_cast<uint8_t>(ctype));
    for (auto b : extra) w.u8(b);
    std::vector<uint8_t> payload(w.data().begin(), w.data().end());
    try { c.send(dpufabric::proto::MsgType::COMMAND, payload); }
    catch (...) { c.close_(); return "<send-fail>"; }
    auto f = c.recv(5000);
    c.close_();
    if (!f || f->type != dpufabric::proto::MsgType::RESULT) return "<no-result>";
    dpufabric::wire::Reader r(f->payload);
    (void)r.u64(); uint8_t ok = r.u8(); std::string detail = r.str();
    return (ok ? "OK:" : "ERR:") + detail;
}

inline std::string admin_create(uint16_t port, uint64_t service_id, int offload_class) {
    std::vector<uint8_t> extra;
    auto push64 = [&](uint64_t v) { for (int i = 0; i < 8; ++i) extra.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF)); };
    push64(service_id); extra.push_back(static_cast<uint8_t>(offload_class));
    return admin(port, 1, extra);
}

} // namespace mptest
