// NovaForge — prompt input box: multi-line editing, history, completion of /commands and @files.
#pragma once
#include "term.hpp"
#ifdef _WIN32
  #include <conio.h>
#endif

namespace nova {

struct Suggestion { std::string value, hint; bool submit = false; };

struct EditorResult { bool ok = false; std::string text; };

class LineEditor {
public:
    // completion: given the text before the cursor, return suggestions and the byte offset of the token they replace.
    std::function<std::vector<Suggestion>(const std::string& before, size_t& tokenStart)> complete;
    std::function<std::string()> footerLeft, footerRight;
    std::function<void()> onShiftTab;   // cycles modes; the footer is redrawn afterwards
    std::function<void()> onClear;      // Ctrl+L
    std::vector<std::string> shortcuts; // shown with "?"

    static fs::path historyFile() { return novaHome() / "history"; }
    void loadHistory() {
        history_.clear();
        std::ifstream f(historyFile()); std::string l;
        while (std::getline(f, l)) if (!l.empty()) history_.push_back(replaceAll(l, "\\n", "\n"));
        if (history_.size() > 500) history_.erase(history_.begin(), history_.end() - 500);
    }
    void addHistory(const std::string& s) {
        if (trim(s).empty()) return;
        if (!history_.empty() && history_.back() == s) return;
        history_.push_back(s);
        std::ofstream f(historyFile(), std::ios::app); f << replaceAll(s, "\n", "\\n") << "\n";
    }

    EditorResult read() {
        if (!Term::interactive()) return readPlain();
        auto& ui = UI::get();
        buf_.clear(); cur_ = 0; histPos_ = (int)history_.size(); draft_.clear();
        sel_ = 0; dismissed_ = false; showHelp_ = false; flash_.clear(); flashUntil_ = 0; drawnRows_ = 0; cursorRow_ = 0;
        Term::raw(true, true);
        std::cout << (ui.color ? "\x1b[?2004h" : "");
        draw();
        EditorResult res; bool done = false;
        long long lastEsc = 0, lastCtrlC = 0;
        while (!done) {
            if (flashUntil_ && nowMs() > flashUntil_) { flash_.clear(); flashUntil_ = 0; draw(); }
            Ev e = next(flashUntil_ ? 150 : -1);
            if (e.k == K::None) continue;
            auto sug = suggestions();
            bool hasSug = !sug.empty() && !dismissed_;
            if (hasSug && sel_ >= (int)sug.size()) sel_ = 0;
            switch (e.k) {
            case K::Text:
                if (e.text == "?" && buf_.empty()) { showHelp_ = !showHelp_; break; }
                insert(e.text); break;
            case K::Paste: insert(e.text); break;
            case K::Enter:
                if (hasSug) {
                    accept(sug[(size_t)sel_]);
                    if (!sug[(size_t)sel_].submit) break;
                }
                if (!buf_.empty() && cur_ == buf_.size() && buf_.back() == "\\") { buf_.back() = "\n"; break; }
                res.ok = true; res.text = text(); done = true; break;
            case K::Newline: insert("\n"); break;
            case K::Tab: if (hasSug) accept(sug[(size_t)sel_]); break;
            case K::ShiftTab: if (onShiftTab) onShiftTab(); break;
            case K::Back: if (cur_ > 0) { buf_.erase(buf_.begin() + (long)cur_ - 1); --cur_; } break;
            case K::Del: if (cur_ < buf_.size()) buf_.erase(buf_.begin() + (long)cur_); break;
            case K::Left: if (cur_ > 0) --cur_; break;
            case K::Right: if (cur_ < buf_.size()) ++cur_; break;
            case K::WordLeft: while (cur_ > 0 && buf_[cur_ - 1] == " ") --cur_; while (cur_ > 0 && buf_[cur_ - 1] != " " && buf_[cur_ - 1] != "\n") --cur_; break;
            case K::WordRight: while (cur_ < buf_.size() && buf_[cur_] == " ") ++cur_; while (cur_ < buf_.size() && buf_[cur_] != " " && buf_[cur_] != "\n") ++cur_; break;
            case K::Home: while (cur_ > 0 && buf_[cur_ - 1] != "\n") --cur_; break;
            case K::End: while (cur_ < buf_.size() && buf_[cur_] != "\n") ++cur_; break;
            case K::KillLeft: { size_t b = cur_; while (b > 0 && buf_[b - 1] != "\n") --b; buf_.erase(buf_.begin() + (long)b, buf_.begin() + (long)cur_); cur_ = b; break; }
            case K::KillRight: { size_t e2 = cur_; while (e2 < buf_.size() && buf_[e2] != "\n") ++e2; buf_.erase(buf_.begin() + (long)cur_, buf_.begin() + (long)e2); break; }
            case K::KillWord: {
                size_t b = cur_; while (b > 0 && buf_[b - 1] == " ") --b; while (b > 0 && buf_[b - 1] != " " && buf_[b - 1] != "\n") --b;
                buf_.erase(buf_.begin() + (long)b, buf_.begin() + (long)cur_); cur_ = b; break; }
            case K::Up:
                if (hasSug) { sel_ = (sel_ + (int)sug.size() - 1) % (int)sug.size(); break; }
                if (!moveRow(-1)) historyStep(-1);
                break;
            case K::Down:
                if (hasSug) { sel_ = (sel_ + 1) % (int)sug.size(); break; }
                if (!moveRow(1)) historyStep(1);
                break;
            case K::Esc:
                if (hasSug) { dismissed_ = true; break; }
                if (showHelp_) { showHelp_ = false; break; }
                if (buf_.empty()) break;
                if (nowMs() - lastEsc < 900) { addHistory(text()); buf_.clear(); cur_ = 0; flash_.clear(); flashUntil_ = 0; }
                else { flash_ = "Esc ещё раз — очистить ввод"; flashUntil_ = nowMs() + 900; }
                lastEsc = nowMs(); break;
            case K::CtrlC:
                if (!buf_.empty()) { addHistory(text()); buf_.clear(); cur_ = 0; break; }
                if (nowMs() - lastCtrlC < 2000) { done = true; res.ok = false; break; }
                flash_ = "Нажмите Ctrl+C ещё раз для выхода"; flashUntil_ = nowMs() + 2000; lastCtrlC = nowMs(); break;
            case K::CtrlD: if (buf_.empty()) { done = true; res.ok = false; } else if (cur_ < buf_.size()) buf_.erase(buf_.begin() + (long)cur_); break;
            case K::CtrlL: erase(); if (onClear) onClear(); drawnRows_ = 0; cursorRow_ = 0; break;
            default: break;
            }
            if (e.k == K::Text || e.k == K::Paste || e.k == K::Back || e.k == K::Del || e.k == K::KillWord || e.k == K::KillLeft || e.k == K::KillRight) {
                dismissed_ = false; sel_ = 0;
            }
            if (!done) draw();
        }
        erase();
        std::cout << (ui.color ? "\x1b[?2004l" : "") << std::flush;
        Term::raw(false);
        if (res.ok) { echo(res.text); addHistory(res.text); }
        return res;
    }

private:
    enum class K { None, Text, Paste, Enter, Newline, Tab, ShiftTab, Back, Del, Left, Right, WordLeft, WordRight, Up, Down, Home, End,
                   KillLeft, KillRight, KillWord, Esc, CtrlC, CtrlD, CtrlL };
    struct Ev { K k = K::None; std::string text; };

    std::vector<std::string> buf_, history_; size_t cur_ = 0; int histPos_ = 0; std::string draft_;
    int sel_ = 0; bool dismissed_ = false, showHelp_ = false;
    std::string flash_; long long flashUntil_ = 0;
    int drawnRows_ = 0, cursorRow_ = 0;

    std::string text() const { std::string s; for (auto& c : buf_) s += c; return s; }
    void setText(const std::string& s) { buf_ = UI::codepoints(s); cur_ = buf_.size(); }
    void insert(const std::string& t) {
        auto cps = UI::codepoints(t);
        for (auto& c : cps) {
            std::string x = c == "\r" ? "\n" : c == "\t" ? "    " : c;
            if ((unsigned char)x[0] < 32 && x != "\n") continue;
            buf_.insert(buf_.begin() + (long)cur_, x); ++cur_;
        }
    }
    void historyStep(int d) {
        if (history_.empty()) return;
        if (histPos_ == (int)history_.size()) draft_ = text();
        int np = histPos_ + d;
        if (np < 0 || np > (int)history_.size()) return;
        histPos_ = np;
        setText(histPos_ == (int)history_.size() ? draft_ : history_[(size_t)histPos_]);
    }

    std::vector<Suggestion> suggestions() const {
        if (!complete || showHelp_) return {};
        std::string before; for (size_t i = 0; i < cur_; ++i) before += buf_[i];
        size_t ts = 0; auto v = complete(before, ts);
        if (v.size() > 8) v.resize(8);
        return v;
    }
    void accept(const Suggestion& s) {
        std::string before; for (size_t i = 0; i < cur_; ++i) before += buf_[i];
        size_t ts = 0; if (complete) complete(before, ts);
        std::string after; for (size_t i = cur_; i < buf_.size(); ++i) after += buf_[i];
        // skip the rest of the current token
        size_t skip = 0; while (skip < after.size() && after[skip] != ' ' && after[skip] != '\n') ++skip;
        std::string nb = before.substr(0, ts) + s.value + (s.submit ? "" : " ");
        size_t ncur = UI::codepoints(nb).size();
        setText(nb + after.substr(skip)); cur_ = ncur; dismissed_ = true;
    }

    // ---------- layout ----------
    struct Layout { std::vector<std::string> rows; int curRow = 0, curCol = 0; };
    int textWidth() const { return std::max(10, UI::cols() - 6); }
    Layout layout() const {
        Layout L; int tw = textWidth();
        std::string row; int w = 0; int r = 0;
        auto flush = [&]() { L.rows.push_back(row); row.clear(); w = 0; ++r; };
        for (size_t i = 0; i <= buf_.size(); ++i) {
            if (i == cur_) { L.curRow = r; L.curCol = w; }
            if (i == buf_.size()) break;
            const std::string& c = buf_[i];
            if (c == "\n") { flush(); continue; }
            int cw = UI::cpWidth(UI::decode(c, 0, c.size()));
            if (w + cw > tw) { flush(); if (i == cur_) { L.curRow = r; L.curCol = 0; } }
            row += c; w += cw;
        }
        L.rows.push_back(row);
        if (L.curCol >= tw) { L.curRow++; L.curCol = 0; if (L.curRow >= (int)L.rows.size()) L.rows.push_back(""); }
        return L;
    }
    bool moveRow(int d) {
        Layout L = layout();
        int target = L.curRow + d;
        if (target < 0 || target >= (int)L.rows.size()) return false;
        // walk to the same column on the target row
        size_t best = cur_; Layout probe;
        size_t save = cur_;
        for (size_t i = 0; i <= buf_.size(); ++i) {
            cur_ = i; probe = layout();
            if (probe.curRow == target && probe.curCol <= L.curCol) best = i;
            if (probe.curRow > target) break;
        }
        cur_ = save; cur_ = best;
        return true;
    }

    std::string borderColor(const std::string& s) const {
        auto& ui = UI::get();
        if (!buf_.empty() && buf_[0] == "!") return ui.warn(s);
        return ui.muted(s);
    }

    void erase() {
        if (cursorRow_ > 0) std::cout << "\x1b[" << cursorRow_ << "A";
        std::cout << "\r\x1b[J" << std::flush;
        cursorRow_ = 0;
    }

    void draw() {
        auto& ui = UI::get();
        int W = UI::cols() - 1;               // keep the last column free: avoids auto-wrap surprises
        int inner = W - 2;
        Layout L = layout();
        int maxRows = std::max(3, termHeight() - 8);
        int first = 0;
        if ((int)L.rows.size() > maxRows) first = std::clamp(L.curRow - maxRows + 1, 0, (int)L.rows.size() - maxRows);
        int last = std::min((int)L.rows.size(), first + maxRows);

        std::vector<std::string> out;
        std::string hz; for (int i = 0; i < inner; ++i) hz += "─";
        out.push_back(borderColor(gl::tl + hz + gl::tr));
        bool bash = !buf_.empty() && buf_[0] == "!";
        for (int r = first; r < last; ++r) {
            std::string pre = r == 0 ? (bash ? ui.warn("! ") : ui.accent("> ")) : "  ";
            std::string body = L.rows[(size_t)r];
            if (r == 0 && bash) body = body.substr(1);
            if (r == 0 && L.rows.size() == 1 && buf_.empty()) body = ui.muted("Напишите задачу, / — команды, @ — файлы");
            std::string content = pre + body;
            int pad = inner - 1 - (int)UI::width(content);
            out.push_back(borderColor("│") + " " + content + std::string((size_t)std::max(0, pad), ' ') + borderColor("│"));
        }
        out.push_back(borderColor(gl::bl + hz + gl::br));

        auto sug = suggestions();
        if (showHelp_) {
            size_t half = (shortcuts.size() + 1) / 2;
            for (size_t i = 0; i < half; ++i) {
                std::string a = shortcuts[i], b = i + half < shortcuts.size() ? shortcuts[i + half] : "";
                out.push_back("  " + ui.muted(UI::pad(a, (size_t)std::max(20, inner / 2 - 2))) + ui.muted(b));
            }
        } else if (!sug.empty() && !dismissed_) {
            size_t vw = 0; for (auto& s : sug) vw = std::max(vw, UI::width(s.value));
            vw = std::min<size_t>(vw + 2, 34);
            for (size_t i = 0; i < sug.size(); ++i) {
                std::string v = UI::pad(ui.fit(sug[i].value, vw - 1), vw);
                std::string h = ui.fit(sug[i].hint, (size_t)std::max(10, inner - (int)vw - 4));
                out.push_back((int)i == sel_ ? "  " + ui.accent(v) + ui.accent(h) : "  " + v + ui.muted(h));
            }
        } else {
            std::string left = !flash_.empty() ? ui.warn(flash_) : footerLeft ? footerLeft() : ui.muted("? — подсказки");
            std::string right = footerRight ? footerRight() : "";
            int gap = inner - (int)UI::width(left) - (int)UI::width(right);
            if (gap < 2) right = ui.fit(right, (size_t)std::max(0, inner - (int)UI::width(left) - 2)), gap = 2;
            out.push_back("  " + left + std::string((size_t)gap, ' ') + right);
        }

        // flicker-free: hide the cursor, overwrite the box in place, clear leftovers once, one write
        std::string s = syncOn() + "\x1b[?25l";
        if (cursorRow_ > 0) s += "\x1b[" + std::to_string(cursorRow_) + "A";
        for (size_t i = 0; i < out.size(); ++i) { s += inPlace(out[i]); if (i + 1 < out.size()) s += "\r\n"; }
        s += "\x1b[J";
        int curLine = 1 + (L.curRow - first);
        int up = (int)out.size() - 1 - curLine;
        if (up > 0) s += "\x1b[" + std::to_string(up) + "A";
        int col = 2 + 2 + L.curCol;   // "│ " + "> "
        s += "\r"; if (col > 0) s += "\x1b[" + std::to_string(col) + "C";
        s += "\x1b[?25h" + syncOff();
        writeFrame(s);
        cursorRow_ = curLine; drawnRows_ = (int)out.size();
    }

    void echo(const std::string& t) {
        auto& ui = UI::get();
        auto lines = split(t, '\n'); bool firstL = true;
        for (auto& l : lines) {
            std::string pre = firstL ? (startsWith(t, "!") ? ui.warn("! ") : ui.muted("> ")) : "  ";
            std::string body = firstL && startsWith(t, "!") ? l.substr(1) : l;
            std::cout << pre << ui.muted(body) << "\n"; firstL = false;
        }
        std::cout << std::flush;
    }

    EditorResult readPlain() {
        EditorResult r; std::string line, b;
        auto& ui = UI::get();
        std::cout << ui.accent("> ") << std::flush;
        while (true) {
            if (!std::getline(std::cin, line)) { r.ok = !trim(b).empty(); r.text = b; return r; }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line.back() == '\\') { line.pop_back(); b += line + "\n"; std::cout << "  " << std::flush; continue; }
            b += line; break;
        }
        r.ok = true; r.text = b; return r;
    }

    // ---------- keys ----------
#ifndef _WIN32
    Ev next(int timeoutMs) {
        Ev e;
        int c = pending_ >= 0 ? pending_ : Term::readByte(timeoutMs);
        pending_ = -1;
        if (c < 0) return e;
        if (c == 27) {
            int c2 = Term::readByte(25);
            if (c2 < 0) { e.k = K::Esc; return e; }
            if (c2 == 13 || c2 == 10) { e.k = K::Newline; return e; }
            if (c2 == 127 || c2 == 8) { e.k = K::KillWord; return e; }
            if (c2 == 'b') { e.k = K::WordLeft; return e; }
            if (c2 == 'f') { e.k = K::WordRight; return e; }
            if (c2 != '[' && c2 != 'O') return e;
            std::string seq; int ch;
            while ((ch = Term::readByte(25)) >= 0) { seq += (char)ch; if (ch >= 0x40 && ch <= 0x7E) break; }
            if (seq == "200~") { e.k = K::Paste; e.text = readPaste(); return e; }
            char fin = seq.empty() ? 0 : seq.back();
            bool ctrl = seq.find(";5") != std::string::npos || seq.find(";3") != std::string::npos;
            if (fin == 'A') e.k = K::Up; else if (fin == 'B') e.k = K::Down;
            else if (fin == 'C') e.k = ctrl ? K::WordRight : K::Right; else if (fin == 'D') e.k = ctrl ? K::WordLeft : K::Left;
            else if (fin == 'H') e.k = K::Home; else if (fin == 'F') e.k = K::End; else if (fin == 'Z') e.k = K::ShiftTab;
            else if (fin == '~') {
                if (seq == "3~") e.k = K::Del; else if (seq == "1~" || seq == "7~") e.k = K::Home; else if (seq == "4~" || seq == "8~") e.k = K::End;
                else if (seq == "13;2~" || seq == "27;2;13~") e.k = K::Newline;
            }
            return e;
        }
        switch (c) {
            case 13: e.k = K::Enter; return e;
            case 10: e.k = K::Newline; return e;
            case 9: e.k = K::Tab; return e;
            case 127: case 8: e.k = K::Back; return e;
            case 1: e.k = K::Home; return e;
            case 5: e.k = K::End; return e;
            case 2: e.k = K::Left; return e;
            case 6: e.k = K::Right; return e;
            case 3: e.k = K::CtrlC; return e;
            case 4: e.k = K::CtrlD; return e;
            case 11: e.k = K::KillRight; return e;
            case 12: e.k = K::CtrlL; return e;
            case 14: e.k = K::Down; return e;
            case 16: e.k = K::Up; return e;
            case 21: e.k = K::KillLeft; return e;
            case 23: e.k = K::KillWord; return e;
            default: break;
        }
        if (c < 32) return e;
        std::string t(1, (char)c);
        size_t len = UI::cpLen((unsigned char)c);
        for (size_t i = 1; i < len; ++i) { int d = Term::readByte(25); if (d < 0) break; t += (char)d; }
        e.k = K::Text; e.text = t;
        // typed-ahead characters (fast typing, terminals without bracketed paste) are taken in one go
        while (Term::keyWaiting(0)) {
            int d = Term::readByte(0);
            if (d < 32 || d == 127) { pending_ = d; break; }
            std::string u(1, (char)d); size_t l2 = UI::cpLen((unsigned char)d);
            for (size_t i = 1; i < l2; ++i) { int x = Term::readByte(25); if (x < 0) break; u += (char)x; }
            e.text += u;
        }
        return e;
    }
    int pending_ = -1;
    std::string readPaste() {
        std::string t; int ch;
        while ((ch = Term::readByte(500)) >= 0) {
            t += (char)ch;
            if (t.size() >= 6 && t.compare(t.size() - 6, 6, "\x1b[201~") == 0) { t.resize(t.size() - 6); break; }
        }
        return t;
    }
#else
    int pending_ = -1;
    static std::string utf8(uint32_t cp) {
        std::string t;
        if (cp < 0x80) t += (char)cp;
        else if (cp < 0x800) { t += (char)(0xC0 | (cp >> 6)); t += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { t += (char)(0xE0 | (cp >> 12)); t += (char)(0x80 | ((cp >> 6) & 0x3F)); t += (char)(0x80 | (cp & 0x3F)); }
        else { t += (char)(0xF0 | (cp >> 18)); t += (char)(0x80 | ((cp >> 12) & 0x3F)); t += (char)(0x80 | ((cp >> 6) & 0x3F)); t += (char)(0x80 | (cp & 0x3F)); }
        return t;
    }
    static uint32_t readCp(wint_t c) {
        if (c >= 0xD800 && c <= 0xDBFF) { wint_t lo = _getwch(); return 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); }
        return c;
    }
    Ev next(int timeoutMs) {
        Ev e;
        wint_t c;
        if (pending_ >= 0) { c = (wint_t)pending_; pending_ = -1; }
        else { if (timeoutMs >= 0 && !Term::keyWaiting(timeoutMs)) return e; c = _getwch(); }
        if (c == 0 || c == 0xE0) {
            wint_t d = _getwch();
            switch (d) {
                case 72: e.k = K::Up; break; case 80: e.k = K::Down; break; case 75: e.k = K::Left; break; case 77: e.k = K::Right; break;
                case 71: e.k = K::Home; break; case 79: e.k = K::End; break; case 83: e.k = K::Del; break;
                case 115: e.k = K::WordLeft; break; case 116: e.k = K::WordRight; break; case 15: e.k = K::ShiftTab; break;
                default: break;
            }
            return e;
        }
        switch (c) {
            case 13: e.k = K::Enter; return e; case 10: e.k = K::Newline; return e; case 9: e.k = K::Tab; return e;
            case 8: e.k = K::Back; return e; case 27: e.k = K::Esc; return e; case 3: e.k = K::CtrlC; return e;
            case 26: case 4: e.k = K::CtrlD; return e; case 1: e.k = K::Home; return e; case 5: e.k = K::End; return e;
            case 21: e.k = K::KillLeft; return e; case 11: e.k = K::KillRight; return e; case 23: e.k = K::KillWord; return e; case 12: e.k = K::CtrlL; return e;
            default: break;
        }
        if (c < 32) return e;
        e.k = K::Text; e.text = utf8(readCp(c));
        // Characters already waiting (fast typing, paste — the classic console has no bracketed
        // paste) are taken in one go: one redraw instead of one per character. Enter in the middle
        // of a long burst with more input behind it is part of a pasted text, not "send".
        size_t burst = 1;
        while (_kbhit()) {
            wint_t d = _getwch();
            if (d == 13 && burst >= 8 && _kbhit()) { e.text += "\n"; ++burst; continue; }
            if (d < 32 || d == 0xE0) { pending_ = (int)d; break; }
            e.text += utf8(readCp(d)); ++burst;
        }
        return e;
    }
#endif
};

} // namespace nova
