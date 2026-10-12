// NovaForge — platform & string utilities
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <random>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <atomic>
#include <cstring>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <shlobj.h>
  #include <knownfolders.h>
  #include <io.h>
  #define popen _popen
  #define pclose _pclose
  #define NOVA_ISATTY(fd) _isatty(fd)
#else
  #include <unistd.h>
  #include <sys/ioctl.h>
  #include <sys/stat.h>
  #include <sys/wait.h>
  #include <poll.h>
  #include <fcntl.h>
  #include <signal.h>
  #include <cerrno>
  #define NOVA_ISATTY(fd) isatty(fd)
#endif

namespace nova {
namespace fs = std::filesystem;

inline std::string env(const char* k, const std::string& def = "") {
    const char* v = std::getenv(k); return v && *v ? std::string(v) : def;
}

inline fs::path homeDir() {
#ifdef _WIN32
    std::string h = env("USERPROFILE"); if (h.empty()) h = env("HOMEDRIVE") + env("HOMEPATH");
#else
    std::string h = env("HOME", "/tmp");
#endif
    return fs::path(h);
}

#ifdef _WIN32
// Known Windows folder (Documents, Roaming AppData…). Handles folders moved to another drive
// or to OneDrive and localized names.
inline fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR w = nullptr; fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &w)) && w) out = fs::path(std::wstring(w));
    if (w) CoTaskMemFree(w);
    return out;
}
// Value written by the Windows installer: HKCU\Software\NovaForge\<name>.
inline fs::path installerSetting(const wchar_t* name) {
    wchar_t buf[2048]; DWORD size = sizeof buf;   // REG_EXPAND_SZ is expanded and returned as REG_SZ
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\NovaForge", name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return fs::path(std::wstring(buf));
}
#endif

// User's Documents folder. On Linux honours XDG user-dirs (e.g. ~/Документы).
inline fs::path documentsDir() {
    fs::path home = homeDir();
#ifdef _WIN32
    fs::path docs = knownFolder(FOLDERID_Documents);
    return docs.empty() ? home / "Documents" : docs;
#else
    std::string xdg = env("XDG_DOCUMENTS_DIR");
    if (xdg.empty()) {
        std::ifstream f(home / ".config" / "user-dirs.dirs"); std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("XDG_DOCUMENTS_DIR=", 0) != 0) continue;
            std::string v = line.substr(18);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            if (v.rfind("$HOME", 0) == 0) v = home.string() + v.substr(5);
            xdg = v; break;
        }
    }
    if (!xdg.empty() && xdg != home.string()) return fs::path(xdg);
    return home / "Documents";
#endif
}

// Settings, keys, sessions, history. Programs files and projects live elsewhere:
//   Windows: %APPDATA%\NovaForge   (an existing %USERPROFILE%\.novaforge from older versions is kept)
//   Linux/macOS: ~/.novaforge
// NOVA_HOME overrides both.
inline fs::path novaHome() {
    static const fs::path p = [] {
        std::string custom = env("NOVA_HOME");
        if (!custom.empty()) return fs::path(custom);
        fs::path legacy = homeDir() / ".novaforge";
#ifdef _WIN32
        std::error_code e;
        if (!fs::exists(legacy, e)) {
            fs::path roaming = knownFolder(FOLDERID_RoamingAppData);
            if (!roaming.empty()) return roaming / "NovaForge";
        }
#endif
        return legacy;
    }();
    std::error_code ec; fs::create_directories(p, ec);
    return p;
}

inline bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}
inline std::string readFile(const fs::path& p) { std::string s; readFile(p, s); return s; }

inline bool writeFile(const fs::path& p, const std::string& data) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc); if (!f) return false;
    f << data; return (bool)f;
}

inline void restrictPerms(const fs::path& p) {
#ifndef _WIN32
    chmod(p.string().c_str(), 0600);
#else
    (void)p;
#endif
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}
inline bool startsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }
inline bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
inline std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == d) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur); return out;
}
inline std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
    return s;
}
inline std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
inline std::string joinStr(const std::vector<std::string>& v, const std::string& sep) {
    std::string o; for (size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; } return o;
}
inline size_t countOccurrences(const std::string& s, const std::string& sub) {
    if (sub.empty()) return 0;
    size_t n = 0, pos = 0;
    while ((pos = s.find(sub, pos)) != std::string::npos) { ++n; pos += sub.size(); }
    return n;
}
// Truncate by UTF-8 codepoints
inline std::string truncateUtf8(const std::string& s, size_t maxCp, const std::string& ell = "…") {
    size_t cp = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (cp == maxCp) return s.substr(0, i) + ell;
        i += len; ++cp;
    }
    return s;
}

inline long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline std::string plural(long long n, const char* one, const char* few, const char* many) {
    long long m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) return one;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
    return many;
}
inline std::string countOf(long long n, const char* one, const char* few, const char* many) { return std::to_string(n) + " " + plural(n, one, few, many); }

inline std::string isoTime(long long ms = 0) {
    if (!ms) ms = nowMs();
    std::time_t t = (std::time_t)(ms / 1000); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32]; std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm); return buf;
}
inline std::string makeId(const std::string& prefix) {
    static std::mt19937_64 rng{std::random_device{}() ^ (unsigned long long)nowMs()};
    const char* al = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s = prefix + "_";
    for (int i = 0; i < 10; ++i) s += al[rng() % 36];
    return s;
}

inline std::string shellQuote(const std::string& s) {
#ifdef _WIN32
    return "\"" + replaceAll(s, "\"", "\\\"") + "\"";
#else
    return "'" + replaceAll(s, "'", "'\\''") + "'";
#endif
}

inline bool validUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len) return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k) if (((unsigned char)s[i + k] >> 6) != 2) return false;
        i += len;
    }
    return true;
}
#ifdef _WIN32
// Console programs (ping, ipconfig, net, git hooks, old compilers…) print in the OEM code page
// (cp866 in Russian Windows) when their output goes to a pipe — that is mojibake in a UTF-8
// terminal and for the model. Text that is not valid UTF-8 is converted from OEM, then ANSI.
inline std::string toUtf8FromLocal(const std::string& s) {
    if (s.empty() || validUtf8(s)) return s;
    for (size_t cut = 1; cut <= 3 && cut < s.size(); ++cut)     // UTF-8 cut off by the output limit
        if (validUtf8(s.substr(0, s.size() - cut))) return s.substr(0, s.size() - cut);
    for (UINT cp : {GetOEMCP(), GetACP()}) {
        int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) continue;
        std::wstring w((size_t)n, L'\0'); MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
        int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
        std::string u((size_t)m, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), n, &u[0], m, nullptr, nullptr);
        return u;
    }
    return s;
}
#else
inline std::string toUtf8FromLocal(const std::string& s) { return s; }
#endif

struct CmdResult { int code = -1; std::string output; bool timedOut = false, cancelled = false; };

// Set by Ctrl+C / Esc while a model turn runs; running commands are stopped.
inline std::atomic<bool>& cancelFlag() { static std::atomic<bool> f{false}; return f; }

// Run a shell command, capture stdout+stderr. onLine (optional) receives streamed lines.
// timeoutMs > 0 stops the command after that time. The command gets no stdin, runs in its
// own process group and is stopped (with all children) on timeout or cancelFlag().
inline CmdResult runCommand(const std::string& cmd, size_t maxBytes = 200000,
                            const std::function<void(const std::string&)>& onLine = nullptr, long long timeoutMs = 0) {
    CmdResult r;
    std::string line; bool truncated = false;
    auto feed = [&](const char* buf, size_t n) {
        if (r.output.size() < maxBytes) r.output.append(buf, std::min(n, maxBytes - r.output.size()));
        if (r.output.size() >= maxBytes && n) truncated = true;
        if (!onLine) return;
        for (size_t i = 0; i < n; ++i) { line += buf[i]; if (buf[i] == '\n') { onLine(toUtf8FromLocal(line)); line.clear(); } }
    };
#ifdef _WIN32
    // CreateProcessW + pipe instead of _popen: UTF-8 command lines (Cyrillic paths), no cmd AutoRun
    // scripts (/d — conda & co. can add seconds to every call), and the command with all its
    // children (job object) can be stopped by Esc or a timeout instead of blocking in fgets.
    auto widen = [](const std::string& u) {
        if (u.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0);
        std::wstring w((size_t)n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), &w[0], n); return w;
    };
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { r.output = "failed to start command"; return r; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
    std::wstring shell = widen(env("COMSPEC", "cmd.exe"));
    std::wstring cl = L"\"" + shell + L"\" /d /s /c \"" + widen(cmd) + L"\"";
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(rd); if (job) CloseHandle(job); r.output = "failed to start command"; return r; }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    long long t0 = (long long)GetTickCount64();
    char buf[8192]; bool killed = false, exited = false; long long killedAt = 0;
    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;      // all writers closed
        if (avail) {
            DWORD n = 0;
            if (!ReadFile(rd, buf, std::min<DWORD>(avail, (DWORD)sizeof buf), &n, nullptr) || n == 0) break;
            feed(buf, n); continue;
        }
        if (exited) break;                                                         // exited and drained
        bool timeUp = timeoutMs > 0 && (long long)GetTickCount64() - t0 > timeoutMs;
        if (!killed && (cancelFlag() || timeUp)) {
            killed = true; killedAt = (long long)GetTickCount64();
            if (timeUp) r.timedOut = true; else r.cancelled = true;
            if (job) TerminateJobObject(job, 130); else TerminateProcess(pi.hProcess, 130);
        }
        if (WaitForSingleObject(pi.hProcess, 15) == WAIT_OBJECT_0) exited = true;  // read what is left, then stop
    }
    if (onLine && !line.empty()) onLine(toUtf8FromLocal(line));
    r.output = toUtf8FromLocal(r.output);
    while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT) {   // output closed, process still running
        bool timeUp = timeoutMs > 0 && (long long)GetTickCount64() - t0 > timeoutMs;
        if (killed) { if ((long long)GetTickCount64() - killedAt > 2000) break; continue; }
        if (cancelFlag() || timeUp) {
            killed = true; killedAt = (long long)GetTickCount64(); if (timeUp) r.timedOut = true; else r.cancelled = true;
            if (job) TerminateJobObject(job, 130); else TerminateProcess(pi.hProcess, 130);
        }
    }
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); r.code = (int)code;
    if (r.cancelled) r.code = 130;
    CloseHandle(pi.hProcess); CloseHandle(rd); if (job) CloseHandle(job);
#else
    int fds[2];
    if (pipe(fds) != 0) { r.output = "failed to start command"; return r; }
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); r.output = "failed to start command"; return r; }
    if (pid == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL); signal(SIGPIPE, SIG_DFL); signal(SIGTTOU, SIG_DFL); signal(SIGTTIN, SIG_DFL);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); close(devnull); }
        dup2(fds[1], 1); dup2(fds[1], 2); close(fds[0]); close(fds[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    long long t0 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto now = [] { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    char buf[8192]; bool killed = false; long long killedAt = 0;
    while (true) {
        pollfd pf{fds[0], POLLIN, 0};
        int pr = poll(&pf, 1, 100);
        if (pr > 0) {
            ssize_t n = read(fds[0], buf, sizeof buf);
            if (n > 0) feed(buf, (size_t)n);
            else if (n == 0 || (errno != EINTR && errno != EAGAIN)) break;
        } else if (pr < 0 && errno != EINTR) break;
        bool timeUp = timeoutMs > 0 && now() - t0 > timeoutMs;
        if (!killed && (cancelFlag() || timeUp)) {
            killed = true; killedAt = now();
            if (timeUp) r.timedOut = true; else r.cancelled = true;
            kill(-pid, SIGTERM);
        }
        if (killed && now() - killedAt > 1500) { kill(-pid, SIGKILL); break; }
    }
    close(fds[0]);
    if (onLine && !line.empty()) onLine(line);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
#endif
    if (truncated) r.output += "\n…[вывод обрезан]";
    return r;
}

inline bool isTTY() { return NOVA_ISATTY(1) != 0; }
inline bool isStdinTTY() { return NOVA_ISATTY(0) != 0; }

// "1.4s", "12s", "2m 05s"
inline std::string fmtDuration(long long ms) {
    char buf[32];
    if (ms < 10000) snprintf(buf, sizeof buf, "%.1fs", ms / 1000.0);
    else if (ms < 60000) snprintf(buf, sizeof buf, "%llds", ms / 1000);
    else snprintf(buf, sizeof buf, "%lldm %02llds", ms / 60000, (ms / 1000) % 60);
    return buf;
}

// Names used as file names (workflows, projects): letters, digits, '-', '_', '.', no leading dot.
inline bool safeName(const std::string& n) {
    if (n.empty() || n.size() > 64 || n[0] == '.') return false;
    for (char c : n) if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

inline int termWidth() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 100;
#else
    struct winsize w{};
    if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_col > 20) return w.ws_col;
    return 100;
#endif
}

inline int termHeight() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 30;
#else
    struct winsize w{};
    if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_row > 5) return w.ws_row;
    return 30;
#endif
}

inline std::string osName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

inline bool isBinaryContent(const std::string& s) {
    size_t n = std::min<size_t>(s.size(), 8000);
    for (size_t i = 0; i < n; ++i) if (s[i] == '\0') return true;
    return false;
}

inline bool isIgnoredDir(const std::string& name) {
    static const char* ign[] = {".git", "node_modules", ".novaforge", "build", "dist", "target", ".venv",
                                "__pycache__", ".idea", ".vscode", ".next", "vendor", ".cache", "bin", "obj"};
    for (auto* i : ign) if (name == i) return true;
    return false;
}

} // namespace nova
