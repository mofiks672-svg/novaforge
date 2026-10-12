// NovaForge — terminal output: ANSI styles, layout helpers, Markdown, spinner, prompts.
//
// Uses only the 16 standard ANSI colours plus bold/dim, so output follows the
// user's own terminal palette. Colours are disabled when stdout is not a TTY,
// when NO_COLOR is set, with --no-color, or with "theme": "mono".
#pragma once
#include "util.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <cstdint>
#include <cstdlib>
#include <cmath>

namespace nova {

struct RGB { int r = 0, g = 0, b = 0; };

inline RGB mix(RGB a, RGB b, double t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return {(int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t)};
}
inline RGB hex(unsigned v) { return {(int)((v >> 16) & 255), (int)((v >> 8) & 255), (int)(v & 255)}; }

// Palettes are taken from physical materials (heated metal, CRT phosphor, steel),
// deliberately avoiding the violet/cyan/pink look typical of generated UIs.
struct Theme {
    std::string name, title;
    std::vector<RGB> grad;          // logo / heading gradient, left to right
    RGB accent, code, ok, warn, err, hot;
    bool ansi = false;              // use the terminal's own 16 colours
    bool mono = false;              // bold/dim only
};

inline const std::vector<Theme>& themes() {
    static const std::vector<Theme> t = [] {
        std::vector<Theme> v;
        v.push_back({"forge", "Кузница — раскалённый металл",
                     {hex(0xFFE7A0), hex(0xFFB347), hex(0xF26B38), hex(0xD2452F), hex(0xA63A2C)},
                     hex(0xF5A04A), hex(0xFFD08A), hex(0x9CC069), hex(0xF2C14E), hex(0xE5533D), hex(0xFFF6D5)});
        v.push_back({"amber", "Янтарный монитор",
                     {hex(0xFFE2A8), hex(0xFFC145), hex(0xFFA000), hex(0xC77700)},
                     hex(0xFFB000), hex(0xFFD27F), hex(0xD7C26B), hex(0xFFCC66), hex(0xFF6A3D), hex(0xFFF3D6)});
        v.push_back({"phosphor", "Зелёный люминофор",
                     {hex(0xD4FFC9), hex(0x7DFF8A), hex(0x2FD06A), hex(0x178F4A)},
                     hex(0x52E37B), hex(0xA6F5B4), hex(0x7DFF8A), hex(0xE3E36B), hex(0xFF6B5B), hex(0xF0FFF0)});
        v.push_back({"steel", "Сталь",
                     {hex(0xF2F2F0), hex(0xC9CCCF), hex(0x9AA0A6), hex(0x6E7378)},
                     hex(0xD8D2C4), hex(0xE6E1D6), hex(0xA8C090), hex(0xE0C080), hex(0xE07060), hex(0xFFFFFF)});
        Theme d; d.name = "default"; d.title = "Цвета терминала"; d.ansi = true;
        d.grad = {hex(0xFFFFFF), hex(0xFFFFFF)}; v.push_back(d);
        Theme m; m.name = "mono"; m.title = "Без цвета"; m.mono = true; m.ansi = true;
        m.grad = d.grad; v.push_back(m);
        return v;
    }();
    return t;
}

// Glyphs. Classic Windows consoles (conhost with Consolas / Lucida Console) have no font fallback:
// anything outside the WGL4 set (▁▂▃, ━, ✓, ❯, ╭, ⎿, ☐…) is drawn as a box with a question mark.
// The "safe" set uses only characters every console font has; "rich" is for modern terminals.
namespace gl {
inline bool rich = true;
inline const char *dot = "●", *elbow = "⎿", *star = "✻", *check = "✓", *cross = "✗", *pointer = "❯",
                  *tl = "╭", *tr = "╮", *bl = "╰", *br = "╯", *barOn = "━", *barOff = "─",
                  *todo = "☐", *todoDone = "☒", *todoActive = "◼", *paused = "⏸", *fast = "⏵⏵";
// Boot-screen spinner frames.
inline const std::vector<const char*>& spinner() {
    static const std::vector<const char*> r = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "▆", "▅", "▃"};
    static const std::vector<const char*> s = {"|", "/", "-", "\\"};
    return rich ? r : s;
}
inline void use(bool richGlyphs) {
    rich = richGlyphs;
    if (rich) {
        dot = "●"; elbow = "⎿"; star = "✻"; check = "✓"; cross = "✗"; pointer = "❯";
        tl = "╭"; tr = "╮"; bl = "╰"; br = "╯"; barOn = "━"; barOff = "─";
        todo = "☐"; todoDone = "☒"; todoActive = "◼"; paused = "⏸"; fast = "⏵⏵";
    } else {
        dot = "●"; elbow = "└"; star = "*"; check = "√"; cross = "×"; pointer = ">";
        tl = "┌"; tr = "┐"; bl = "└"; br = "┘"; barOn = "═"; barOff = "─";
        todo = "[ ]"; todoDone = "[x]"; todoActive = "[>]"; paused = "||"; fast = ">>";
    }
}
// auto: rich everywhere except a classic Windows console (Windows Terminal sets WT_SESSION,
// VS Code and others set TERM_PROGRAM). NOVA_GLYPHS or "glyphs" in config.json override it.
inline bool autoRich() {
#ifdef _WIN32
    return !env("WT_SESSION").empty() || !env("TERM_PROGRAM").empty() || !env("ConEmuANSI").empty() || !env("ALACRITTY_LOG").empty();
#else
    return true;
#endif
}
inline void configure(const std::string& setting) {
    std::string v = lower(env("NOVA_GLYPHS", setting));
    use(v == "rich" ? true : v == "safe" ? false : autoRich());
}
}

// ---------- flicker-free output ----------
// Clearing a line and then drawing it again ("\r\x1b[2K" + text) makes the console show an empty
// line for a moment — that is the blinking. Instead every redraw:
//   * overwrites lines in place and erases only what is left to the right of the new text (EL0);
//   * removes stale lines below the frame with a single "\x1b[J" at the very end;
//   * is sent as ONE write (on Windows straight to the console, the CRT would split it into chunks);
//   * is wrapped in synchronized output (DEC mode 2026: Windows Terminal 1.23+, WezTerm, kitty,
//     iTerm2, foot, Alacritty…), so the terminal shows the frame only when it is complete.
//     Terminals without it ignore the sequence.
inline const std::string& syncOn()  { static const std::string s = "\x1b[?2026h"; return s; }
inline const std::string& syncOff() { static const std::string s = "\x1b[?2026l"; return s; }
inline void writeFrame(const std::string& s) {
#ifdef _WIN32
    std::cout.flush(); std::fflush(stdout);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        const char* p = s.data(); size_t left = s.size();
        while (left) {
            DWORD w = 0;
            if (!WriteFile(h, p, (DWORD)std::min<size_t>(left, (size_t)1 << 20), &w, nullptr) || w == 0) break;
            p += w; left -= w;
        }
        return;
    }
#endif
    std::cout.write(s.data(), (std::streamsize)s.size());
    std::cout.flush();
}
// One terminal line rewritten in place: "\r" + text + reset + erase-to-end-of-line.
inline std::string inPlace(const std::string& text) { return "\r" + text + "\x1b[0m\x1b[K"; }

#ifdef _WIN32
// A classic console window (conhost: PowerShell, cmd, shortcuts) may use the raster font
// ("Raster Fonts" / "Точечные шрифты"). It cannot show UTF-8: Cyrillic and box characters become
// garbage. Switch such a window to a TrueType font of the same height for the session and put the
// original font back on exit. Windows Terminal and other terminals are not affected.
inline void fixRasterFont(HANDLE h) {
    static CONSOLE_FONT_INFOEX orig{};
    CONSOLE_FONT_INFOEX f{}; f.cbSize = sizeof f;
    if (!GetCurrentConsoleFontEx(h, FALSE, &f)) return;
    if (f.FontFamily & TMPF_TRUETYPE) return;                  // already TrueType
    orig = f;
    CONSOLE_FONT_INFOEX n{}; n.cbSize = sizeof n;
    n.dwFontSize.X = 0;
    n.dwFontSize.Y = f.dwFontSize.Y >= 12 ? f.dwFontSize.Y + 4 : 16;   // raster 8x12 ≈ TrueType 16
    n.FontFamily = FF_DONTCARE | TMPF_TRUETYPE; n.FontWeight = FW_NORMAL;
    wcscpy(n.FaceName, L"Consolas");
    if (!SetCurrentConsoleFontEx(h, FALSE, &n)) { wcscpy(n.FaceName, L"Lucida Console"); SetCurrentConsoleFontEx(h, FALSE, &n); }
    std::atexit([] { HANDLE o = GetStdHandle(STD_OUTPUT_HANDLE); if (o != INVALID_HANDLE_VALUE) SetCurrentConsoleFontEx(o, FALSE, &orig); });
}
#endif

class UI {
    std::vector<std::string> recent_;
public:
    static UI& get() { static UI ui; return ui; }

    enum class Depth { Basic, X256, True };
    bool color = true;
    Depth depth = Depth::True;
    Theme theme = themes()[0];
    std::string themeName = "forge";

    void init() {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
        fixRasterFont(h);
        depth = Depth::True;
#else
        std::string ct = lower(env("COLORTERM")), term = lower(env("TERM")), prog = env("TERM_PROGRAM");
        if (ct.find("truecolor") != std::string::npos || ct.find("24bit") != std::string::npos ||
            prog == "iTerm.app" || prog == "vscode" || prog == "WezTerm" || !env("WT_SESSION").empty() || !env("KITTY_WINDOW_ID").empty())
            depth = Depth::True;
        else if (term.find("256") != std::string::npos) depth = Depth::X256;
        else if (prog == "Apple_Terminal") depth = Depth::X256;
        else depth = Depth::Basic;
#endif
        if (!isTTY() || !env("NO_COLOR").empty() || lower(env("TERM")) == "dumb") color = false;
        if (!env("FORCE_COLOR").empty()) color = true;
    }
    static std::vector<std::string> themeNames() {
        std::vector<std::string> v; for (auto& t : themes()) v.push_back(t.name); return v;
    }
    // Unknown names fall back to "forge".
    bool setTheme(const std::string& n) {
        for (auto& t : themes()) if (t.name == n) { theme = t; themeName = n; return true; }
        theme = themes()[0]; themeName = theme.name;
        return false;
    }
    bool rgbOk() const { return color && !theme.ansi && depth != Depth::Basic; }

    // ---------- styles ----------
    std::string sgr(const char* code, const std::string& s) const {
        if (!color || s.empty()) return s;
        return std::string("\x1b[") + code + "m" + s + "\x1b[0m";
    }
    static int to256(RGB c) {
        auto lvl = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
        int r = lvl(c.r), g = lvl(c.g), b = lvl(c.b);
        int gray = (c.r + c.g + c.b) / 3;
        if (std::abs(c.r - c.g) < 10 && std::abs(c.g - c.b) < 10 && gray > 8 && gray < 238) return 232 + (gray - 8) / 10;
        return 16 + 36 * r + 6 * g + b;
    }
    std::string fgCode(RGB c) const {
        if (depth == Depth::True) return "\x1b[38;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
        return "\x1b[38;5;" + std::to_string(to256(c)) + "m";
    }
    std::string rgb(RGB c, const std::string& s, bool b = false) const {
        if (!rgbOk() || s.empty()) return b ? bold(s) : s;
        return (b ? std::string("\x1b[1m") : "") + fgCode(c) + s + "\x1b[0m";
    }
    std::string role(RGB c, const char* ansi, const std::string& s) const {
        if (!color || theme.mono) return s;
        if (rgbOk()) return rgb(c, s);
        return sgr(ansi, s);
    }

    std::string bold(const std::string& s) const   { return sgr("1", s); }
    std::string dim(const std::string& s) const    { return sgr("2", s); }
    std::string italic(const std::string& s) const { return sgr("3", s); }
    std::string accent(const std::string& s) const { return role(theme.accent, "33", s); }
    std::string code(const std::string& s) const   { return role(theme.code, "36", s); }
    std::string ok(const std::string& s) const     { return role(theme.ok, "32", s); }
    std::string warn(const std::string& s) const   { return role(theme.warn, "33", s); }
    std::string err(const std::string& s) const    { return role(theme.err, "31", s); }
    std::string muted(const std::string& s) const  { return dim(s); }
    std::string strong(const std::string& s) const { return bold(s); }
    std::string reset() const { return color ? "\x1b[0m" : ""; }

    RGB gradAt(double t) const { return gradAt(theme.grad, t); }
    // Bright part of the gradient, for short text (headings, selected menu items).
    std::vector<RGB> headGrad() const {
        if (theme.grad.size() <= 2) return theme.grad;
        return std::vector<RGB>(theme.grad.begin(), theme.grad.end() - (theme.grad.size() > 3 ? 2 : 1));
    }
    std::string heading(const std::string& s) const { return gradient(s, headGrad(), true); }
    static RGB gradAt(const std::vector<RGB>& g, double t) {
        if (g.empty()) return {255, 255, 255};
        if (g.size() == 1) return g[0];
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        double x = t * (double)(g.size() - 1); size_t i = (size_t)x; if (i >= g.size() - 1) i = g.size() - 2;
        return mix(g[i], g[i + 1], x - (double)i);
    }
    // Horizontal gradient over visible characters.
    std::string gradient(const std::string& s, bool b = true) const { return gradient(s, theme.grad, b); }
    std::string gradient(const std::string& s, const std::vector<RGB>& g, bool b = true) const {
        if (!rgbOk()) return b ? bold(theme.ansi ? s : accent(s)) : accent(s);
        auto cps = codepoints(s); std::string out = b ? "\x1b[1m" : "";
        size_t n = cps.size() > 1 ? cps.size() - 1 : 1;
        for (size_t i = 0; i < cps.size(); ++i) out += (cps[i] == " " ? "" : fgCode(gradAt(g, (double)i / (double)n))) + cps[i];
        return out + "\x1b[0m";
    }

    // ---------- text measurement ----------
    static size_t cpLen(unsigned char c) {
        return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    }
    static uint32_t decode(const std::string& s, size_t i, size_t len) {
        unsigned char c = (unsigned char)s[i];
        if (len == 1 || i + len > s.size()) return c;
        uint32_t cp = len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (size_t k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        return cp;
    }
    // Display columns of one codepoint (0 for combining marks, 2 for wide CJK / emoji).
    static int cpWidth(uint32_t cp) {
        if (cp == 0) return 0;
        if ((cp >= 0x0300 && cp <= 0x036F) || cp == 0x200B || cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F)) return 0;
        if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
            (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
            (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD)) return 2;
        return 1;
    }
    // Length of an ANSI escape sequence starting at i (0 if none).
    static size_t escLen(const std::string& s, size_t i) {
        if (s[i] != '\x1b') return 0;
        size_t j = i + 1;
        if (j < s.size() && s[j] == '[') {
            ++j;
            while (j < s.size() && !((unsigned char)s[j] >= 0x40 && (unsigned char)s[j] <= 0x7E)) ++j;
            return j < s.size() ? j - i + 1 : s.size() - i;
        }
        return j < s.size() ? 2 : 1;
    }
    static std::vector<std::string> codepoints(const std::string& s) {
        std::vector<std::string> out;
        for (size_t i = 0; i < s.size();) { size_t len = cpLen((unsigned char)s[i]); out.push_back(s.substr(i, len)); i += len; }
        return out;
    }
    // Visible width: skips ANSI escapes, accounts for wide characters.
    static size_t width(const std::string& s) {
        size_t w = 0;
        for (size_t i = 0; i < s.size();) {
            if (size_t e = escLen(s, i)) { i += e; continue; }
            size_t len = cpLen((unsigned char)s[i]);
            w += (size_t)cpWidth(decode(s, i, len)); i += len;
        }
        return w;
    }
    // Cut to at most `cols` visible columns without breaking escape sequences.
    std::string fit(const std::string& s, size_t cols, const std::string& ell = "…") const {
        if (width(s) <= cols) return s;
        if (cols == 0) return "";
        size_t budget = cols - width(ell), w = 0; std::string out; bool styled = false;
        for (size_t i = 0; i < s.size();) {
            if (size_t e = escLen(s, i)) { out += s.substr(i, e); styled = true; i += e; continue; }
            size_t len = cpLen((unsigned char)s[i]);
            int cw = cpWidth(decode(s, i, len));
            if (w + (size_t)cw > budget) break;
            out += s.substr(i, len); w += (size_t)cw; i += len;
        }
        return out + (styled ? reset() : "") + ell;
    }
    static std::string pad(const std::string& s, size_t cols) {
        size_t w = width(s); return w >= cols ? s + " " : s + std::string(cols - w, ' ');
    }
    static int cols() { return std::max(40, termWidth()); }
    // Word wrap by display width (plain text).
    static std::vector<std::string> wrap(const std::string& text, size_t w) {
        std::vector<std::string> out;
        for (auto& para : split(text, '\n')) {
            std::string line; size_t lw = 0;
            for (auto& word : split(para, ' ')) {
                size_t ww = width(word);
                if (lw && lw + 1 + ww > w) { out.push_back(line); line.clear(); lw = 0; }
                if (lw) { line += " "; ++lw; }
                line += word; lw += ww;
            }
            out.push_back(line);
        }
        return out;
    }

    // ---------- layout ----------
    void rule() const { std::string s; int w = std::min(cols(), 100); for (int i = 0; i < w; ++i) s += "─"; std::cout << dim(s) << "\n"; }

    // Bold heading followed by indented lines. No boxes, no decoration.
    void section(const std::string& title, const std::vector<std::string>& lines) const {
        if (!title.empty()) std::cout << "\n" << bold(title) << "\n";
        size_t max = (size_t)cols() - 3;
        for (auto& l : lines) std::cout << (l.empty() ? "" : "  " + fit(l, max)) << "\n";
    }
    // Two-column key/value list with aligned keys.
    void keyValues(const std::string& title, const std::vector<std::pair<std::string, std::string>>& kv) const {
        size_t kw = 0; for (auto& p : kv) kw = std::max(kw, width(p.first));
        std::vector<std::string> lines;
        for (auto& p : kv) lines.push_back(p.first.empty() && p.second.empty() ? "" : muted(pad(p.first, kw + 2)) + p.second);
        section(title, lines);
    }

    void info(const std::string& s) const    { std::cout << "  " << s << "\n"; }
    void note(const std::string& s) const    { std::cout << "  " << muted(s) << "\n"; }
    void success(const std::string& s) { remember("  " + ok(gl::check) + " " + s); std::cout << recent_.back() << "\n"; }
    void warning(const std::string& s) { remember("  " + bold(warn("!")) + " " + s); std::cout << recent_.back() << "\n"; }
    void error(const std::string& s)   { remember("  " + bold(err(gl::cross)) + " " + s); std::cerr << recent_.back() << "\n"; }

    // Full-screen menus clear the terminal; the last status lines are shown again on the next screen.
    void remember(const std::string& line) { recent_.push_back(line); if (recent_.size() > 3) recent_.erase(recent_.begin()); }
    void forget() { recent_.clear(); }
    void replay() { if (recent_.empty()) return; std::cout << "\n"; for (auto& l : recent_) std::cout << l << "\n"; recent_.clear(); }
    void clearScreen() const { if (color && isTTY()) std::cout << "\x1b[2J\x1b[H" << std::flush; }

    // ---------- inline markdown -> ANSI ----------
    std::string inlineMd(const std::string& s) const {
        if (!color) return replaceAll(s, "**", "");
        std::string out; size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '`') {
                size_t j = s.find('`', i + 1);
                if (j != std::string::npos) { out += code(s.substr(i + 1, j - i - 1)); i = j + 1; continue; }
            }
            if (s.compare(i, 2, "**") == 0) {
                size_t j = s.find("**", i + 2);
                if (j != std::string::npos && j > i + 2) { out += bold(s.substr(i + 2, j - i - 2)); i = j + 2; continue; }
            }
            out += s[i++];
        }
        return out;
    }

};

// Streaming Markdown renderer: feed text deltas, renders complete lines.
class MarkdownStream {
public:
    void feed(const std::string& d) {
        buf_ += d; size_t pos;
        while ((pos = buf_.find('\n')) != std::string::npos) { renderLine(buf_.substr(0, pos)); buf_.erase(0, pos + 1); }
    }
    void flush() { if (!trim(buf_).empty()) renderLine(buf_); buf_.clear(); inCode_ = false; blank_ = true; bullet_ = false; std::cout.flush(); }
    // The first printed line of the next answer starts with ⏺ instead of the indent.
    void bullet() { bullet_ = true; }
private:
    std::string buf_; bool inCode_ = false; bool blank_ = true, bullet_ = false; int codeBlank_ = 0;
    void out(const std::string& line) {
        if (bullet_ && !line.empty()) {
            auto& ui = UI::get(); bullet_ = false;
            std::string l = startsWith(line, "  ") ? line.substr(2) : line;
            std::cout << ui.bold(gl::dot) << " " << l << "\n"; blank_ = false; return;
        }
        if (bullet_ && line.empty()) return;
        std::cout << line << "\n"; blank_ = line.empty();
    }
    void blank() { if (!blank_) out(""); }
    void renderLine(std::string l) {
        auto& ui = UI::get();
        if (!l.empty() && l.back() == '\r') l.pop_back();
        std::string t = trim(l);
        if (startsWith(t, "```")) {
            inCode_ = !inCode_; codeBlank_ = 0;
            std::string lang = trim(t.substr(3));
            if (inCode_ && !lang.empty() && lang != "text") out("  " + ui.muted(lang));
            return;
        }
        if (inCode_) {
            if (t.empty()) { ++codeBlank_; return; }        // emit blank code lines only between content
            for (; codeBlank_ > 0; --codeBlank_) out("  " + ui.muted("│"));
            out("  " + ui.muted("│") + " " + l); return;
        }
        if (startsWith(t, "#### ")) out("  " + ui.bold(t.substr(5)));
        else if (startsWith(t, "### ")) out("  " + ui.bold(ui.accent(t.substr(4))));
        else if (startsWith(t, "## ")) { blank(); out("  " + ui.heading(t.substr(3))); }
        else if (startsWith(t, "# ")) { blank(); out("  " + ui.heading(t.substr(2))); }
        else if (startsWith(t, "- ") || startsWith(t, "* ")) {
            size_t ind = l.find_first_not_of(' ');
            out("  " + std::string(ind == std::string::npos ? 0 : ind, ' ') + ui.accent("•") + " " + ui.inlineMd(t.substr(2)));
        }
        else if (startsWith(t, "> ")) out("  " + ui.muted("│ " + t.substr(2)));
        else if (t == "---" || t == "***") out("  " + ui.muted("────"));
        else if (t.empty()) blank();
        else out("  " + ui.inlineMd(l));
        std::cout.flush();
    }
};

// Rounded box around lines (used for the welcome card and permission prompts).
inline void box(const std::vector<std::string>& lines, RGB border, int width = 0) {
    auto& ui = UI::get();
    int W = width > 0 ? width : std::min(UI::cols() - 1, 96);
    int inner = W - 2;
    std::string hz; for (int i = 0; i < inner; ++i) hz += "─";
    auto b = [&](const std::string& x) { return ui.rgbOk() ? ui.rgb(border, x) : ui.muted(x); };
    std::cout << b(gl::tl + hz + gl::tr) << "\n";
    for (auto& l : lines) {
        std::string c = ui.fit(l, (size_t)inner - 2);
        int pad = inner - 1 - (int)UI::width(c);
        std::cout << b("│") << " " << c << std::string((size_t)std::max(0, pad), ' ') << b("│") << "\n";
    }
    std::cout << b(gl::bl + hz + gl::br) << "\n";
}

// Activity line in the style of the conversation view:
//   ✻ Кую ответ… (12s · ↓ 1.2k · esc — прервать)
// The glyph breathes, a highlight runs over the text.
class Spinner {
public:
    void start(const std::string& msg, const std::string& tail = "") {
        if (!isTTY()) return;
        stop(); running_ = true; msg_ = msg; tail_ = tail;
        th_ = std::thread([this] {
            auto& ui = UI::get(); long long t0 = nowMs(); int i = 0;
            static const char* fancy[] = {"·", "✢", "✳", "✶", "✻", "✽", "✻", "✶", "✳", "✢"};
            static const char* plain[] = {"·", "+", "*", "*", "*", "*", "*", "*", "*", "+"};
            const char** frames = gl::rich ? fancy : plain;
            while (running_) {
                std::string g = frames[(i / 2) % 10];
                std::string text; { std::lock_guard<std::mutex> lk(mu_); text = msg_ + "…"; }
                auto cps = codepointsOf(text);
                std::string shown;
                if (ui.rgbOk()) {
                    // highlight moving left to right with a soft tail
                    double pos = std::fmod(i * 0.55, (double)cps.size() + 8.0) - 4.0;
                    for (size_t k = 0; k < cps.size(); ++k) {
                        double d = std::fabs((double)k - pos);
                        double t = d < 3.5 ? 1.0 - d / 3.5 : 0.0;
                        shown += ui.fgCode(mix(ui.theme.accent, ui.theme.hot, t)) + cps[k];
                    }
                    shown += "\x1b[0m";
                } else shown = ui.accent(text);
                long long s = (nowMs() - t0) / 1000;
                std::string info = std::to_string(s) + "s";
                { std::lock_guard<std::mutex> lk(mu_); if (!tail_.empty()) info += " · " + tail_; }
                info += " · esc — прервать";
                std::string gl = ui.rgbOk() ? ui.rgb(mix(ui.theme.accent, ui.theme.hot, 0.5 + 0.5 * std::sin(i * 0.35)), g) : ui.accent(g);
                std::string line = gl + " " + shown + " " + ui.muted("(" + info + ")");
                { std::lock_guard<std::mutex> lk(out_); writeFrame(syncOn() + "\x1b[?25l" + inPlace(ui.fit(line, (size_t)UI::cols() - 2)) + syncOff()); }
                ++i;
                for (int k = 0; k < 6 && running_; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(16));   // ~100 ms per frame
            }
            std::lock_guard<std::mutex> lk(out_);
            writeFrame("\r\x1b[K\x1b[?25h");
        });
    }
    void update(const std::string& msg) { std::lock_guard<std::mutex> lk(mu_); msg_ = msg; }
    void tail(const std::string& t) { std::lock_guard<std::mutex> lk(mu_); tail_ = t; }
    bool running() const { return running_; }
    void stop() { if (th_.joinable()) { running_ = false; th_.join(); } running_ = false; }
    ~Spinner() { stop(); }
private:
    static std::vector<std::string> codepointsOf(const std::string& s) { return UI::codepoints(s); }
    std::atomic<bool> running_{false}; std::thread th_; std::string msg_, tail_; std::mutex mu_, out_;
};

inline std::string ask(const std::string& prompt, const std::string& def = "") {
    auto& ui = UI::get();
    std::cout << "  " << ui.bold(prompt);
    if (!def.empty()) std::cout << ui.muted(" [" + def + "]");
    std::cout << ": " << std::flush;
    std::string s; if (!std::getline(std::cin, s)) { std::cout << "\n"; return def; }
    s = trim(s); return s.empty() ? def : s;
}


} // namespace nova
