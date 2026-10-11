// NovaForge — raw keyboard input and arrow-key menus.
#pragma once
#include "ui.hpp"
#include <csignal>
#ifdef _WIN32
  #include <conio.h>
#else
  #include <termios.h>
  #include <poll.h>
#endif

namespace nova {

enum class Key { None, Up, Down, Left, Right, Enter, Escape, Back, Home, End, Char };
struct KeyEvent { Key key = Key::None; char ch = 0; };

class Term {
public:
    static bool interactive() { return isTTY() && isStdinTTY(); }

#ifndef _WIN32
    static termios& saved() { static termios t{}; return t; }
    static bool& active() { static bool a = false; return a; }
    static bool& fullRaw() { static bool f = false; return f; }
    // full = also deliver Ctrl+C, Ctrl+J, Ctrl+S… as bytes (used by the line editor).
    static void raw(bool on, bool full = false) {
        if (on && active() && fullRaw() != full) { tcsetattr(0, TCSANOW, &saved()); active() = false; }
        if (on == active()) return;
        if (on) {
            if (tcgetattr(0, &saved()) != 0) return;
            termios t = saved();
            t.c_lflag &= ~(ICANON | ECHO);
            if (full) { t.c_lflag &= ~(ISIG | IEXTEN); t.c_iflag &= ~(IXON | ICRNL); }
            t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
            tcsetattr(0, TCSANOW, &t);
            fullRaw() = full;
        } else tcsetattr(0, TCSANOW, &saved());
        active() = on;
    }
    // Called from the SIGINT handler (tcsetattr is async-signal-safe).
    static void restoreFromSignal() { if (active()) tcsetattr(0, TCSANOW, &saved()); }
    static bool keyWaiting(int ms) { pollfd p{0, POLLIN, 0}; return poll(&p, 1, ms) > 0; }
    static int readByte(int ms = -1) {
        if (ms >= 0 && !keyWaiting(ms)) return -1;
        unsigned char c; return ::read(0, &c, 1) == 1 ? c : -1;
    }
#else
    static void raw(bool, bool = false) {}
    static void restoreFromSignal() {}
    static bool keyWaiting(int ms) {
        for (int i = 0; i <= ms; i += 10) { if (_kbhit()) return true; if (ms) Sleep(10); }
        return _kbhit() != 0;
    }
    static int readByte(int ms = -1) {
        if (ms >= 0 && !keyWaiting(ms)) return -1;
        return _getch();
    }
#endif

    static KeyEvent readKey() {
        KeyEvent e;
#ifdef _WIN32
        int c = _getch();
        if (c == 0 || c == 224) {
            int d = _getch();
            e.key = d == 72 ? Key::Up : d == 80 ? Key::Down : d == 75 ? Key::Left : d == 77 ? Key::Right : d == 71 ? Key::Home : d == 79 ? Key::End : Key::None;
            return e;
        }
        if (c == 13) { e.key = Key::Enter; return e; }
        if (c == 27) { e.key = Key::Escape; return e; }
        if (c == 8) { e.key = Key::Back; return e; }
        if (c == 3) { std::raise(SIGINT); return e; }
        e.key = Key::Char; e.ch = (char)c; return e;
#else
        int c = readByte();
        if (c < 0) return e;
        if (c == 27) {
            int c2 = readByte(30);
            if (c2 < 0) { e.key = Key::Escape; return e; }
            if (c2 == '[' || c2 == 'O') {
                int c3 = readByte(30);
                if (c3 >= '0' && c3 <= '9') { int t = readByte(30); (void)t; e.key = c3 == '1' || c3 == '7' ? Key::Home : c3 == '4' || c3 == '8' ? Key::End : Key::None; return e; }
                e.key = c3 == 'A' ? Key::Up : c3 == 'B' ? Key::Down : c3 == 'C' ? Key::Right : c3 == 'D' ? Key::Left : c3 == 'H' ? Key::Home : c3 == 'F' ? Key::End : Key::None;
                return e;
            }
            e.key = Key::Escape; return e;
        }
        if (c == '\r' || c == '\n') { e.key = Key::Enter; return e; }
        if (c == 127 || c == 8) { e.key = Key::Back; return e; }
        if (c == 3) { raw(false); std::raise(SIGINT); return e; }
        if (c == 4) { e.key = Key::Escape; return e; }
        e.key = Key::Char; e.ch = (char)c; return e;
#endif
    }

    // Drain pending input (e.g. a key pressed to skip the intro).
    static void drain() {
#ifndef _WIN32
        bool was = active(); raw(true);
        while (keyWaiting(0)) readByte(0);
        if (!was) raw(false);
#else
        while (_kbhit()) _getch();
#endif
    }
    static void hideCursor(bool h) { if (UI::get().color) std::cout << (h ? "\x1b[?25l" : "\x1b[?25h") << std::flush; }
};

// Yes/no question. In a terminal it is answered with a single key (Y/Д/Enter or N/Esc),
// without a line prompt; Russian layout keys (Н = Y, Т = N) work too.
inline bool confirm(const std::string& prompt, bool def) {
    auto& ui = UI::get();
    if (!Term::interactive()) {
        std::string a = lower(ask(prompt + (def ? " (Y/n)" : " (y/N)")));
        if (a.empty()) return def;
        return a == "y" || a == "yes" || a == "д" || a == "да";
    }
    std::cout << "  " << ui.bold(prompt) << ui.muted(def ? "  [Y/n] " : "  [y/N] ") << std::flush;
    int r = -1; // 1 yes, 0 no
#ifdef _WIN32
    while (r < 0) {
        wint_t c = _getwch();
        if (c == 0 || c == 0xE0) { _getwch(); continue; }
        if (c == 13) r = def ? 1 : 0;
        else if (c == 27) r = 0;
        else if (c == 3) { std::raise(SIGINT); r = 0; }
        else if (c == L'y' || c == L'Y' || c == 0x434 || c == 0x414 || c == 0x43D || c == 0x41D) r = 1;  // д Д н Н
        else if (c == L'n' || c == L'N' || c == 0x442 || c == 0x422) r = 0;                            // т Т
    }
#else
    bool was = Term::active(); Term::raw(true);
    while (r < 0) {
        int c = Term::readByte();
        if (c < 0) { r = def ? 1 : 0; break; }
        if (c == '\r' || c == '\n') r = def ? 1 : 0;
        else if (c == 27) { while (Term::keyWaiting(15)) Term::readByte(0); r = 0; }
        else if (c == 3 || c == 4) { if (!was) Term::raw(false); if (c == 3) std::raise(SIGINT); r = 0; }
        else if (c == 'y' || c == 'Y') r = 1;
        else if (c == 'n' || c == 'N') r = 0;
        else if (c == 0xD0 || c == 0xD1) {
            int d = Term::readByte(50);
            if (c == 0xD0 && (d == 0xB4 || d == 0x94 || d == 0xBD || d == 0x9D)) r = 1;   // д Д н Н
            else if ((c == 0xD1 && d == 0x82) || (c == 0xD0 && d == 0xA2)) r = 0;          // т Т
        }
    }
    if (!was) Term::raw(false);
#endif
    std::cout << (r ? ui.ok("да") : ui.muted("нет")) << "\n" << std::flush;
    return r == 1;
}

struct MenuItem {
    std::string label, hint;
    bool separator = false;
    bool disabled = false;
};

// Arrow-key menu. Returns the selected index, or -1 on Esc / q.
// Long lists scroll inside the visible screen. filterable: typing narrows the list
// (model lists of OpenRouter & co. have hundreds of entries).
// Falls back to a numbered prompt when there is no terminal.
inline int menu(const std::string& title, const std::vector<MenuItem>& items, int def = 0, const std::string& footer = "", bool filterable = false) {
    auto& ui = UI::get();
    std::vector<int> selectableAll;
    for (size_t i = 0; i < items.size(); ++i) if (!items[i].separator && !items[i].disabled) selectableAll.push_back((int)i);
    if (selectableAll.empty()) return -1;
    if (std::find(selectableAll.begin(), selectableAll.end(), def) == selectableAll.end()) def = selectableAll[0];

    if (!Term::interactive()) {
        if (!title.empty()) std::cout << ui.bold(title) << "\n";
        int n = 0; std::map<int, int> num;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].separator) { std::cout << "\n"; continue; }
            if (items[i].disabled) continue;
            num[++n] = (int)i;
            std::cout << "  " << n << ") " << items[i].label << (items[i].hint.empty() ? "" : "  " + items[i].hint) << "\n";
        }
        std::cout << "Номер (пусто — назад): " << std::flush;
        std::string s; if (!std::getline(std::cin, s)) return -1;
        try { int k = std::stoi(trim(s)); if (num.count(k)) return num[k]; } catch (...) {}
        return -1;
    }

    size_t labelW = 0; for (auto& it : items) labelW = std::max(labelW, UI::width(it.label));
    labelW = std::min<size_t>(labelW, (size_t)std::max(20, UI::cols() / 2));
    std::string query;
    std::vector<int> visible, selectable;
    auto refilter = [&]() {
        visible.clear(); selectable.clear();
        std::string q = lower(query);
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            if (!q.empty() && (it.separator || it.disabled || lower(it.label).find(q) == std::string::npos)) continue;
            visible.push_back((int)i);
            if (!it.separator && !it.disabled) selectable.push_back((int)i);
        }
    };
    refilter();
    int cur = def, lines = 0, top = 0;
    Term::raw(true); Term::hideCursor(true);
    auto draw = [&](bool first) {
        std::string out = syncOn();
        if (!first && lines) out += "\x1b[" + std::to_string(lines) + "A";
        lines = 0;
        auto line = [&](const std::string& s) { out += inPlace(s) + "\r\n"; ++lines; };
        if (!title.empty()) line("  " + ui.heading(title));
        if (filterable) line("  " + ui.muted("Поиск: ") + query + ui.accent("_") +
                             ui.muted(query.empty() ? "  начните печатать" : "  " + std::to_string(selectable.size()) + " из " + std::to_string(selectableAll.size())));
        int fixed = lines + 4;                               // indicators, blank line, footer
        int room = std::max(3, termHeight() - fixed - 1);
        int total = (int)visible.size();
        int curPos = 0; for (int k = 0; k < total; ++k) if (visible[(size_t)k] == cur) curPos = k;
        if (total <= room) top = 0;
        else {
            if (curPos < top) top = curPos;
            if (curPos >= top + room) top = curPos - room + 1;
            top = std::clamp(top, 0, total - room);
        }
        int end = std::min(total, top + room);
        if (total > room) line(top > 0 ? "    " + ui.muted("↑ ещё " + std::to_string(top)) : "");
        for (int k = top; k < end; ++k) {
            size_t i = (size_t)visible[(size_t)k];
            const auto& it = items[i];
            if (it.separator) { line(it.label.empty() ? "" : "    " + ui.muted(it.label)); continue; }
            std::string lab = UI::pad(ui.fit(it.label, labelW), labelW + 1);
            std::string hint = ui.fit(it.hint, (size_t)std::max(0, UI::cols() - (int)labelW - 8));
            if ((int)i == cur) line("  " + ui.accent(gl::pointer) + " " + ui.heading(lab) + hint);
            else if (it.disabled) line("    " + ui.muted(lab + hint));
            else line("    " + lab + ui.muted(hint));
        }
        if (total == 0) line("    " + ui.muted("ничего не найдено"));
        if (total > room) line(end < total ? "    " + ui.muted("↓ ещё " + std::to_string(total - end)) : "");
        line("");
        std::string f = footer.empty() ? (filterable ? "↑↓ выбрать   Enter — открыть   Esc — сбросить поиск / назад" : "↑↓ выбрать   Enter — открыть   Esc — назад") : footer;
        line("  " + ui.muted(f));
        out += "\x1b[J" + syncOff();
        writeFrame(out);
    };
    draw(true);
    int result = -1;
    while (true) {
        KeyEvent k = Term::readKey();
        auto pos = std::find(selectable.begin(), selectable.end(), cur) - selectable.begin();
        bool has = !selectable.empty();
        if (pos >= (long)selectable.size() && has) { cur = selectable[0]; pos = 0; }
        if (k.key == Key::Up || (!filterable && k.key == Key::Char && k.ch == 'k')) { if (has) cur = selectable[(size_t)((pos + (long)selectable.size() - 1) % (long)selectable.size())]; }
        else if (k.key == Key::Down || (!filterable && k.key == Key::Char && k.ch == 'j')) { if (has) cur = selectable[(size_t)((pos + 1) % (long)selectable.size())]; }
        else if (k.key == Key::Home) { if (has) cur = selectable.front(); }
        else if (k.key == Key::End) { if (has) cur = selectable.back(); }
        else if (k.key == Key::Enter || (!filterable && k.key == Key::Right)) { if (has) { result = cur; break; } }
        else if (k.key == Key::Escape && filterable && !query.empty()) { query.clear(); refilter(); }
        else if (k.key == Key::Escape || (k.key == Key::Left && query.empty()) || (!filterable && k.key == Key::Char && k.ch == 'q')) { result = -1; break; }
        else if (filterable && k.key == Key::Back) {
            if (!query.empty()) { do query.pop_back(); while (!query.empty() && ((unsigned char)query.back() & 0xC0) == 0x80); refilter(); }
        }
        else if (filterable && k.key == Key::Char && (unsigned char)k.ch >= 32) {
            query += k.ch; refilter();
            if (!selectable.empty() && std::find(selectable.begin(), selectable.end(), cur) == selectable.end()) cur = selectable[0];
        }
        else if (!filterable && k.key == Key::Char && k.ch >= '1' && k.ch <= '9') {
            size_t n = (size_t)(k.ch - '1'); if (n < selectable.size()) { cur = selectable[n]; draw(false); result = cur; break; }
        }
        else if (k.key == Key::None) continue;
        draw(false);
    }
    Term::raw(false); Term::hideCursor(false);
    return result;
}

inline int choose(const std::string& prompt, const std::vector<std::string>& opts, int def = 0) {
    std::vector<MenuItem> items; for (auto& o : opts) items.push_back({o, ""});
    int r = menu(prompt, items, def, "↑↓ выбрать   Enter — подтвердить");
    return r < 0 ? def : r;
}

} // namespace nova
