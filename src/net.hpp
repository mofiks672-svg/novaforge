// NovaForge — minimal plain-HTTP client on sockets for local servers (Ollama, LM Studio, llama.cpp).
// Probing them through curl cost a process start per request and, on Windows, ~2 s per closed
// port (connection refused is retried). Here a closed port fails immediately and a slow host is
// capped by a short connect timeout. HTTPS still goes through curl.
#pragma once
#include "util.hpp"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using sock_t = SOCKET;
  #define NOVA_CLOSE closesocket
  #define NOVA_BADSOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <signal.h>
  using sock_t = int;
  #define NOVA_CLOSE ::close
  #define NOVA_BADSOCK (-1)
#endif

namespace nova {
namespace net {

inline void initSockets() {
#ifdef _WIN32
    static bool done = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    (void)done;
#else
    static bool done = [] { signal(SIGPIPE, SIG_IGN); return true; }();
    (void)done;
#endif
}

struct Url { std::string scheme, host, path = "/"; int port = 0; };

inline bool parseUrl(const std::string& url, Url& u) {
    size_t p = url.find("://"); if (p == std::string::npos) return false;
    u.scheme = lower(url.substr(0, p));
    std::string rest = url.substr(p + 3);
    size_t slash = rest.find_first_of("/?#");
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (!u.path.empty() && u.path[0] != '/') u.path = "/" + u.path;
    if (auto at = hostport.rfind('@'); at != std::string::npos) hostport = hostport.substr(at + 1);
    if (!hostport.empty() && hostport[0] == '[') {
        size_t e = hostport.find(']'); if (e == std::string::npos) return false;
        u.host = hostport.substr(1, e - 1);
        if (e + 1 < hostport.size() && hostport[e + 1] == ':') u.port = std::atoi(hostport.c_str() + e + 2);
    } else {
        size_t c = hostport.rfind(':');
        u.host = c == std::string::npos ? hostport : hostport.substr(0, c);
        if (c != std::string::npos) u.port = std::atoi(hostport.c_str() + c + 1);
    }
    if (!u.port) u.port = u.scheme == "https" ? 443 : 80;
    return !u.host.empty();
}

namespace detail {
inline void setNonBlocking(sock_t s, bool on) {
#ifdef _WIN32
    u_long m = on ? 1 : 0; ioctlsocket(s, FIONBIO, &m);
#else
    int f = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, on ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK));
#endif
}
// Wait until the socket is readable (write = false) or writable. false on timeout.
inline bool waitFor(sock_t s, bool write, int ms) {
    fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs);
    fd_set ex; FD_ZERO(&ex); FD_SET(s, &ex);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    int r = select((int)s + 1, write ? nullptr : &fs, write ? &fs : nullptr, &ex, &tv);
    return r > 0;
}
inline sock_t connectTo(const Url& u, int timeoutMs, std::string& err) {
    std::string host = lower(u.host) == "localhost" ? "127.0.0.1" : u.host;
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) { err = "не удалось найти адрес " + u.host; return NOVA_BADSOCK; }
    sock_t s = NOVA_BADSOCK;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == NOVA_BADSOCK) continue;
#ifdef _WIN32
        // A refused connection is otherwise retried for ~2 s before connect() gives up.
  #ifdef SIO_TCP_INITIAL_RTO
        TCP_INITIAL_RTO_PARAMETERS rto{TCP_INITIAL_RTO_UNSPECIFIED_RTT, TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS};
        DWORD got = 0; WSAIoctl(s, SIO_TCP_INITIAL_RTO, &rto, sizeof rto, nullptr, 0, &got, nullptr, nullptr);
  #endif
#endif
        int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        setNonBlocking(s, true);
        int rc = ::connect(s, a->ai_addr, (int)a->ai_addrlen);
        bool ok = rc == 0;
        if (!ok) {
#ifdef _WIN32
            bool pending = WSAGetLastError() == WSAEWOULDBLOCK;
#else
            bool pending = errno == EINPROGRESS;
#endif
            if (pending && waitFor(s, true, timeoutMs)) {
                int e = 0; socklen_t l = sizeof e;
                getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&e, &l);
                ok = e == 0;
                if (!ok) err = "соединение отклонено";
            } else err = pending ? "нет ответа" : "соединение отклонено";
        }
        if (ok) { freeaddrinfo(res); return s; }
        NOVA_CLOSE(s); s = NOVA_BADSOCK;
    }
    freeaddrinfo(res);
    if (err.empty()) err = "нет соединения";
    return NOVA_BADSOCK;
}
inline std::string dechunk(const std::string& b) {
    std::string out; size_t i = 0;
    while (i < b.size()) {
        size_t e = b.find("\r\n", i); if (e == std::string::npos) break;
        size_t n = 0; try { n = std::stoul(b.substr(i, e - i), nullptr, 16); } catch (...) { break; }
        if (n == 0) break;
        i = e + 2; if (i + n > b.size()) { out += b.substr(i); break; }
        out += b.substr(i, n); i += n + 2;
    }
    return out;
}
} // namespace detail

// GET over plain HTTP. Returns the HTTP status, 0 if there is no connection (err explains why).
inline int httpGet(const std::string& url, const std::vector<std::string>& headers, std::string& body,
                   int connectMs = 400, int totalMs = 4000, std::string* err = nullptr) {
    body.clear();
    Url u; if (!parseUrl(url, u) || u.scheme != "http") { if (err) *err = "поддерживается только http://"; return 0; }
    initSockets();
    std::string e;
    sock_t s = detail::connectTo(u, connectMs, e);
    if (s == NOVA_BADSOCK) { if (err) *err = e; return 0; }
    std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) +
                      "\r\nAccept: application/json\r\nConnection: close\r\n";
    for (auto& h : headers) req += h + "\r\n";
    req += "\r\n";
    long long t0 = nowMs();
    size_t sent = 0;
    while (sent < req.size()) {
        if (!detail::waitFor(s, true, 1000)) break;
        int n = (int)::send(s, req.data() + sent, (int)(req.size() - sent), 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    if (sent < req.size()) { NOVA_CLOSE(s); if (err) *err = "не удалось отправить запрос"; return 0; }
    std::string raw; char buf[16384];
    size_t headEnd = std::string::npos, need = std::string::npos; bool chunked = false;
    while (true) {
        int left = (int)(totalMs - (nowMs() - t0));
        if (left <= 0 || !detail::waitFor(s, false, left)) break;
        int n = (int)::recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        raw.append(buf, (size_t)n);
        if (headEnd == std::string::npos && (headEnd = raw.find("\r\n\r\n")) != std::string::npos) {
            std::string h = lower(raw.substr(0, headEnd));
            chunked = h.find("transfer-encoding: chunked") != std::string::npos;
            size_t cl = h.find("content-length:");
            if (cl != std::string::npos) need = headEnd + 4 + (size_t)std::atoll(h.c_str() + cl + 15);
        }
        if (need != std::string::npos && raw.size() >= need) break;
        if (chunked && raw.size() >= 5 && raw.compare(raw.size() - 5, 5, "0\r\n\r\n") == 0) break;
    }
    NOVA_CLOSE(s);
    if (headEnd == std::string::npos) { if (err) *err = raw.empty() ? "сервер не ответил" : "некорректный ответ"; return 0; }
    int code = 0;
    { size_t sp = raw.find(' '); if (sp != std::string::npos && sp < headEnd) code = std::atoi(raw.c_str() + sp + 1); }
    body = raw.substr(headEnd + 4);
    if (chunked) body = detail::dechunk(body);
    return code;
}

} // namespace net
} // namespace nova
