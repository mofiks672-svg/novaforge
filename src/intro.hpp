// NovaForge — startup: boot checks, forged-logo animation, static logo.
#pragma once
#include "term.hpp"
#include "config.hpp"
#include <cmath>
#include <memory>

namespace nova {

// Animation tempo. All intro timings go through animMs(): the base values are already ~1.5x
// slower than in 1.4.0; "animation_speed" in config (or NOVA_ANIM_SPEED) scales them further:
// 1.0 = default, 0.7 = even slower, 1.5 = faster, 0 = no delays at all.
inline double animSpeed() {
    static double s = [] {
        std::string e = env("NOVA_ANIM_SPEED");
        double v = 1.0;
        try { v = e.empty() ? Config::get().data.getNum("animation_speed", 1.0) : std::stod(e); } catch (...) { v = 1.0; }
        return v <= 0 ? 0.0 : std::clamp(v, 0.2, 5.0);
    }();
    return s;
}
inline int animMs(int ms) { double s = animSpeed(); return s <= 0 ? 0 : (int)std::lround(ms / s); }
inline void animSleep(int ms) { int d = animMs(ms); if (d > 0) std::this_thread::sleep_for(std::chrono::milliseconds(d)); }

// 5x5 pixel font, only the letters we need.
inline const std::vector<std::string>& glyph(char c) {
    static const std::map<char, std::vector<std::string>> f = {
        {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#"}},
        {'O', {".###.", "#...#", "#...#", "#...#", ".###."}},
        {'V', {"#...#", "#...#", "#...#", ".#.#.", "..#.."}},
        {'A', {".###.", "#...#", "#####", "#...#", "#...#"}},
        {'F', {"#####", "#....", "####.", "#....", "#...."}},
        {'R', {"####.", "#...#", "####.", "#..#.", "#...#"}},
        {'G', {".####", "#....", "#..##", "#...#", ".####"}},
        {'E', {"#####", "#....", "####.", "#....", "#####"}},
        {' ', {"..", "..", "..", "..", ".."}},
    };
    static const std::vector<std::string> blank = f.at(' ');
    auto it = f.find(c); return it == f.end() ? blank : it->second;
}

struct Bitmap {
    int w = 0, h = 5; std::vector<std::string> rows;
    bool on(int x, int y) const { return y >= 0 && y < h && x >= 0 && x < (int)rows[(size_t)y].size() && rows[(size_t)y][(size_t)x] == '#'; }
};

inline Bitmap logoBitmap() {
    Bitmap b; b.rows.assign(5, "");
    std::string word = "NOVA FORGE";
    for (size_t i = 0; i < word.size(); ++i) {
        auto& g = glyph(word[i]);
        for (int y = 0; y < 5; ++y) b.rows[(size_t)y] += g[(size_t)y] + (i + 1 < word.size() ? "." : "");
    }
    b.w = (int)b.rows[0].size();
    return b;
}

struct Spark { double x, y, vx, vy; int life, max; int kind = 0; };   // kind: 0 spark, 1 ember, 2 steam

class Intro {
public:
    enum class Status { Ok, Warn, Fail };
    struct Step { std::string label; std::function<std::pair<Status, std::string>()> run; };

    // Pixel width in terminal columns: 2 on wide terminals, 1 otherwise.
    static int scale() { Bitmap b = logoBitmap(); return UI::cols() >= b.w * 2 + 8 ? 2 : 1; }
    static int indent() { Bitmap b = logoBitmap(); int w = b.w * scale(); return std::max(2, (UI::cols() - w) / 2 > 4 ? 4 : 2); }

    static RGB target(const Bitmap& b, int x, int y) { return mix(UI::get().gradAt((double)x / (b.w - 1)), {0, 0, 0}, y * 0.06); }

    // Final, cooled logo.
    static void printLogo() {
        auto& ui = UI::get(); Bitmap b = logoBitmap(); int sc = scale();
        std::string pad(indent(), ' ');
        for (int y = 0; y < b.h; ++y) {
            std::string line = pad;
            for (int x = 0; x < b.w; ++x) {
                std::string px; for (int k = 0; k < sc; ++k) px += b.on(x, y) ? "█" : " ";
                if (!b.on(x, y)) { line += px; continue; }
                line += ui.rgbOk() ? ui.fgCode(target(b, x, y)) + px : px;
            }
            std::cout << line << ui.reset() << "\n";
        }
    }

    static void tagline(bool animate) {
        auto& ui = UI::get();
        std::string left = "AI-кузница кода в терминале", right = "v" + std::string(VERSION);
        int width = logoBitmap().w * scale();
        int gap = std::max(2, width - (int)UI::width(left) - (int)UI::width(right));
        std::string pad(indent(), ' ');
        if (!animate || !ui.rgbOk()) {
            if (ui.rgbOk()) {
                std::string line = pad; for (int x = 0; x < width; ++x) line += ui.fgCode(mix(ui.gradAt((double)x / std::max(1, width - 1)), {0, 0, 0}, 0.45)) + "─";
                std::cout << line << ui.reset() << "\n";
            } else std::cout << pad << ui.muted(std::string(""))  << "\n";
            std::cout << pad << ui.muted(left) << std::string((size_t)gap, ' ') << ui.accent(right) << "\n";
            return;
        }
        // underline grows from the centre, then the tagline is typed with a glowing cursor
        int half = width / 2;
        for (int r = 0; r <= half; r += 2) {
            std::string line = pad;
            for (int x = 0; x < width; ++x) {
                int d = std::abs(x - half);
                if (d > r) { line += " "; continue; }
                RGB c = ui.gradAt((double)x / std::max(1, width - 1));
                double edge = r - d < 3 ? 1.0 - (r - d) / 3.0 : 0.0;
                line += ui.fgCode(mix(mix(c, {0, 0, 0}, 0.45), ui.theme.hot, edge * 0.8)) + "─";
            }
            std::cout << "\r" << line << ui.reset() << std::flush;
            if (Term::keyWaiting(0)) break;
            animSleep(14);
        }
        std::string line = pad; for (int x = 0; x < width; ++x) line += ui.fgCode(mix(ui.gradAt((double)x / std::max(1, width - 1)), {0, 0, 0}, 0.45)) + "─";
        std::cout << "\r" << line << ui.reset() << "\n";
        auto cps = UI::codepoints(left); std::string typed;
        bool skip = false;
        for (size_t i = 0; i < cps.size(); ++i) {
            typed += cps[i];
            if (!skip && Term::keyWaiting(0)) skip = true;
            if (skip) continue;
            std::cout << "\r" << pad << ui.muted(typed) << ui.rgb(ui.theme.hot, "▌") << std::flush;
            animSleep(16);
        }
        std::cout << "\r" << pad << ui.muted(left) << std::string((size_t)gap, ' ');
        for (auto& cp : UI::codepoints(right)) {
            std::cout << ui.rgb(ui.theme.hot, cp) << std::flush;
            if (!skip) animSleep(28);
        }
        writeFrame(inPlace(pad + ui.muted(left) + std::string((size_t)gap, ' ') + ui.accent(right)) + "\n");
    }

    // Boot screen: real checks with a spinner each, a glowing progress bar below.
    // All checks start at once in the background; the list only reveals them in order,
    // so the screen takes as long as the slowest check, not the sum of all of them.
    // Returns false if the user skipped (any key).
    static bool boot(const std::vector<Step>& steps) {
        auto& ui = UI::get();
        const auto& spin = gl::spinner();
        std::cout << "\x1b[2J\x1b[H\n  " << ui.gradient("NOVAFORGE", true) << ui.muted("  разжигаю горы") << "\n\n";
        size_t lw = 0; for (auto& s : steps) lw = std::max(lw, UI::width(s.label));
        bool skipped = false; int tick = 0;
        int bw = std::min(48, UI::cols() - 18);
        auto bar = [&](double p, const std::string& label) {
            int fill = (int)std::lround(bw * p); std::string out;
            RGB last{-1, -1, -1};
            for (int i = 0; i < bw; ++i) {
                if (i < fill) {
                    if (!ui.rgbOk()) { out += gl::barOn; continue; }
                    RGB c = ui.gradAt((double)i / bw);
                    int d = fill - 1 - i; double glow = d < 4 ? 1.0 - d / 4.0 : 0.0;
                    double flick = 0.12 * std::sin(tick * 0.9 + i * 1.7);
                    c = mix(c, ui.theme.hot, std::clamp(glow + flick * glow, 0.0, 1.0));
                    if (c.r != last.r || c.g != last.g || c.b != last.b) { out += ui.fgCode(c); last = c; }
                    out += gl::barOn;
                } else { if (i == fill && ui.rgbOk()) out += "\x1b[0m\x1b[2m"; out += gl::barOff; }
            }
            char pct[16]; snprintf(pct, sizeof pct, "%3d%%", (int)std::lround(p * 100));
            return "  " + out + ui.reset() + "  " + ui.muted(std::string(pct) + "  " + label);
        };
        size_t n = steps.size();
        std::vector<std::pair<Status, std::string>> res(n);
        std::unique_ptr<std::atomic<bool>[]> done(new std::atomic<bool>[n]);
        for (size_t i = 0; i < n; ++i) done[i] = false;
        std::vector<std::thread> workers;
        for (size_t i = 0; i < n; ++i)
            workers.emplace_back([&, i] {
                try { res[i] = steps[i].run(); } catch (const std::exception& e) { res[i] = {Status::Fail, e.what()}; }
                done[i] = true;
            });
        std::cout << "\n" << bar(0, "") << "\x1b[1A\r" << std::flush;   // bar sits one line below the step list
        for (size_t si = 0; si < n; ++si) {
            auto& s = steps[si];
            long long t0 = nowMs();
            while (!done[si] || (!skipped && nowMs() - t0 < animMs(80))) {
                if (!skipped && Term::keyWaiting(0)) skipped = true;
                double frac = std::min(0.9, (nowMs() - t0) / std::max(1.0, animMs(400) * 1.0));
                std::string line = "  " + ui.muted("[ ") + ui.accent(spin[(size_t)tick % spin.size()]) + ui.muted("    ]") + " " + UI::pad(s.label, lw + 3);
                writeFrame(syncOn() + inPlace(line) + "\r\n" + inPlace("") + "\r\n" + inPlace(bar((si + frac) / n, s.label)) + "\x1b[2A\r" + syncOff());
                ++tick;
                if (skipped) std::this_thread::sleep_for(std::chrono::milliseconds(1)); else animSleep(34);
            }
            auto [st, detail] = res[si];
            std::string tag = st == Status::Ok ? ui.ok("  OK  ") : st == Status::Warn ? ui.warn(" WARN ") : ui.err(" FAIL ");
            writeFrame(syncOn() + inPlace("  " + ui.muted("[") + tag + ui.muted("]") + " " + UI::pad(s.label, lw + 3) +
                                          ui.muted(ui.fit(detail, (size_t)std::max(10, UI::cols() - (int)lw - 18)))) + "\r\n" +
                       inPlace("") + "\r\n" + inPlace(bar((double)(si + 1) / n, si + 1 == n ? "" : steps[si + 1].label)) + "\x1b[1A\r" + syncOff());
        }
        for (auto& w : workers) w.join();
        // finish: the bar flashes white-hot and settles
        for (int f = 0; f < (skipped ? 1 : 7); ++f) {
            ++tick;
            std::string b = bar(1.0, "");
            if (ui.rgbOk() && f < 6) {
                double t = 1.0 - f / 6.0; std::string out;
                for (int i = 0; i < bw; ++i) out += ui.fgCode(mix(ui.gradAt((double)i / bw), ui.theme.hot, t * (0.5 + 0.5 * std::sin(i * 0.4 - f)))) + gl::barOn;
                b = "  " + out + ui.reset() + "  " + ui.muted("100%");
            }
            writeFrame(syncOn() + inPlace("") + "\r\n" + inPlace(b + "  " + ui.ok(f == 0 && !skipped ? "" : "готово")) + "\x1b[1A\r" + syncOff());
            if (!skipped) animSleep(40);
        }
        std::cout << "\n\n" << std::flush;
        if (!skipped) animSleep(220);
        return !skipped;
    }

    // The logo is forged: cold metal heats up, every letter is struck by a hammer
    // (flash, sparks, a small shake), then it is quenched (steam) and polished (a glint).
    static void forge() {
        auto& ui = UI::get(); Bitmap b = logoBitmap(); int sc = scale();
        const int top = 3, bottom = 2, H = b.h + top + bottom;
        std::vector<std::pair<int, int>> letters;     // pixel columns of each letter
        { int x = 0; std::string word = "NOVA FORGE";
          for (char ch : word) { int w = (int)glyph(ch)[0].size(); if (ch != ' ') letters.push_back({x, x + w - 1}); x += w + 1; } }
        // The full-length forge (as before 1.4.0): ~3.7 s at the default tempo, 30 ms per frame.
        // Every letter gets its own hammer blow; heating, quenching and the glint are unhurried.
        const int HEAT = 24, STEP = 6, STRIKE0 = HEAT, STRIKES = (int)letters.size() * STEP + 4;
        const int QUENCH0 = STRIKE0 + STRIKES, QUENCH = 22, SHINE0 = QUENCH0 + QUENCH, SHINE = 18, END = SHINE0 + SHINE;
        std::vector<Spark> parts; std::mt19937 rng(7); std::uniform_real_distribution<double> U(0, 1);
        RGB hot = ui.theme.hot, ember = ui.theme.accent, cold = hex(0x2A0F0B), red = hex(0x9A2614), orange = hex(0xFF6A24);
        RGB steamC = hex(0xC8C4BE);
        auto letterOf = [&](int x) { for (size_t i = 0; i < letters.size(); ++i) if (x >= letters[i].first && x <= letters[i].second) return (int)i; return -1; };
        auto heatColor = [&](double h) { return h < 0.5 ? mix(cold, red, h * 2) : mix(red, orange, (h - 0.5) * 2); };

        std::cout << "\x1b[?7l";
        for (int i = 0; i < H; ++i) std::cout << "\n";
        for (int f = 0; f <= END; ++f) {
            if (Term::keyWaiting(0)) break;
            int shake = 0;
            // ---- particles ----
            if (f < STRIKE0 + 6) for (int k = 0; k < 2; ++k) if (U(rng) < 0.45)
                parts.push_back({U(rng) * b.w, (double)(H - 1), (U(rng) - 0.5) * 0.25, -(0.18 + U(rng) * 0.22), 0, 14 + (int)(U(rng) * 12), 1});
            int struckNow = -1;
            if (f >= STRIKE0 && f < STRIKE0 + (int)letters.size() * STEP && (f - STRIKE0) % STEP == 0) struckNow = (f - STRIKE0) / STEP;
            if (struckNow >= 0) {
                shake = 1;
                auto [x0, x1] = letters[(size_t)struckNow];
                int count = 10 + (int)(U(rng) * 8);
                for (int k = 0; k < count; ++k) {
                    double x = x0 + U(rng) * (x1 - x0 + 1); bool up = U(rng) < 0.75;
                    double sp = 0.4 + U(rng) * 0.9, ang = (U(rng) - 0.5) * 2.4;
                    parts.push_back({x, up ? (double)top - 0.5 : (double)(top + b.h), std::sin(ang) * sp * 1.6, up ? -std::cos(ang) * sp : std::cos(ang) * sp * 0.4, 0, 7 + (int)(U(rng) * 10), 0});
                }
            }
            if (f >= QUENCH0 && f < QUENCH0 + QUENCH - 6) for (int k = 0; k < 3; ++k) if (U(rng) < 0.6)
                parts.push_back({U(rng) * b.w, (double)top + U(rng) * 2, (U(rng) - 0.5) * 0.3, -(0.12 + U(rng) * 0.2), 0, 10 + (int)(U(rng) * 8), 2});

            // ---- logo pixels ----
            struct Cell { std::string px; RGB c{0, 0, 0}; bool lit = false; };
            std::vector<std::vector<Cell>> cells((size_t)H, std::vector<Cell>((size_t)b.w, Cell{std::string((size_t)sc, ' ')}));
            auto put = [&](int x, int y, const std::string& ch, RGB c, bool fillScale = true) {
                std::string px; if (fillScale) for (int k = 0; k < sc; ++k) px += ch; else px = ch + std::string((size_t)(sc - 1), ' ');
                // quantized colours give long runs of equal colour: far fewer escape codes per frame
                c = {std::min(255, (c.r + 2) / 5 * 5), std::min(255, (c.g + 2) / 5 * 5), std::min(255, (c.b + 2) / 5 * 5)};
                cells[(size_t)y][(size_t)x] = Cell{px, c, true};
            };
            for (int y = 0; y < b.h; ++y)
                for (int x = 0; x < b.w; ++x) {
                    if (!b.on(x, y)) continue;
                    double noise = (U(rng) - 0.5) * 0.18;
                    int li = letterOf(x);
                    int struckAt = STRIKE0 + li * STEP;
                    RGB c; std::string ch;
                    if (f < HEAT || f < struckAt) {
                        double h = std::clamp((double)f / HEAT * 0.85 + noise, 0.0, 1.0);
                        if (f >= HEAT) h = std::clamp(0.85 + noise, 0.0, 1.0);
                        c = heatColor(h); ch = h < 0.3 ? "░" : h < 0.6 ? "▒" : "▓";
                    } else if (f < QUENCH0) {
                        int t = f - struckAt;
                        c = t < 2 ? RGB{255, 255, 255} : mix(hot, hex(0xFFB347), std::min(1.0, (t - 2) / 10.0) + noise * 0.5);
                        ch = "█";
                    } else {
                        double q = std::clamp((f - QUENCH0) / (double)QUENCH * 1.4 - (double)x / b.w * 0.4, 0.0, 1.0);
                        c = mix(mix(hot, hex(0xFFB347), 1.0), target(b, x, y), q); ch = "█";
                        if (f >= SHINE0) {
                            double pos = (f - SHINE0) * (b.w + 10.0) / SHINE - 5.0;
                            double d = std::fabs(x + y * 1.2 - pos);
                            if (d < 2.5) c = mix(target(b, x, y), {255, 250, 235}, 0.75 * (1 - d / 2.5));
                            else c = target(b, x, y);
                        }
                    }
                    put(x, y + top, ch, c);
                }
            // hammer above the next letter
            if (f >= STRIKE0 - 2 && f < STRIKE0 + (int)letters.size() * STEP) {
                int li = (f - STRIKE0 + 2) / STEP, ph = (f - STRIKE0 + 2) % STEP;
                if (li < (int)letters.size() && ph < 2) {
                    auto [x0, x1] = letters[(size_t)li];
                    int hy = top - 2 + ph;
                    for (int x = x0; x <= x1; ++x) if (hy >= 0) put(x, hy, ph == 0 ? "▄" : "█", hex(0x8C8F94));
                }
            }
            for (auto& p : parts) {
                int gx = (int)std::lround(p.x), gy = (int)std::lround(p.y);
                if (gx < 0 || gx >= b.w || gy < 0 || gy >= H || p.life >= p.max) continue;
                if (gy >= top && gy < top + b.h && b.on(gx, gy - top)) continue;
                double t = (double)p.life / p.max;
                if (p.kind == 0) put(gx, gy, t < 0.25 ? "*" : t < 0.6 ? "+" : "·", mix(hot, mix(ember, hex(0x3A1A10), 0.6), t), false);
                else if (p.kind == 1) put(gx, gy, t < 0.5 ? "·" : "˙", mix(mix(ember, red, 0.4), cold, t), false);
                else put(gx, gy, t < 0.35 ? "▒" : t < 0.7 ? "░" : "·", mix(steamC, hex(0x2E2C2A), t), true);
            }
            for (auto& p : parts) {
                p.x += p.vx; p.y += p.vy; ++p.life;
                if (p.kind == 0) p.vy += 0.09; else if (p.kind == 2) p.vx *= 0.95;
            }
            parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Spark& p) { return p.life >= p.max; }), parts.end());

            std::string pad((size_t)(indent() + shake), ' ');
            std::string out = syncOn() + "\x1b[" + std::to_string(H) + "A";
            for (int y = 0; y < H; ++y) {
                out += "\r" + pad;
                RGB last{-1, -1, -1};
                for (auto& c : cells[(size_t)y]) {
                    if (c.lit && ui.rgbOk() && (c.c.r != last.r || c.c.g != last.g || c.c.b != last.b)) { out += ui.fgCode(c.c); last = c.c; }
                    out += c.px;
                }
                out += ui.rgbOk() ? "\x1b[0m\x1b[K\r\n" : "\x1b[K\r\n";
            }
            out += syncOff();
            long long ft = nowMs();
            writeFrame(out);
            long long spent = nowMs() - ft;   // slow consoles: do not add a full frame delay on top of a slow write
            int frame = animMs(30);
            if (spent < frame) std::this_thread::sleep_for(std::chrono::milliseconds(frame - spent));
        }
        // final frame: clean, cooled logo
        std::cout << "\x1b[" << H << "A";
        for (int i = 0; i < top; ++i) std::cout << "\r\x1b[2K\n";
        printLogo();
        for (int i = 0; i < bottom; ++i) std::cout << "\r\x1b[2K\n";
        std::cout << "\x1b[" << bottom << "A\x1b[?7h" << std::flush;
    }

    // A short glint over an already printed logo (cursor must be right below it).
    static void shine() {
        auto& ui = UI::get(); if (!ui.rgbOk()) return;
        Bitmap b = logoBitmap(); int sc = scale(); std::string pad(indent(), ' ');
        for (int f = 0; f <= 16; ++f) {
            if (Term::keyWaiting(0)) break;
            double pos = f * (b.w + 10.0) / 16 - 5.0;
            std::string out = syncOn() + "\x1b[" + std::to_string(b.h) + "A";
            for (int y = 0; y < b.h; ++y) {
                out += "\r" + pad;
                for (int x = 0; x < b.w; ++x) {
                    std::string px; for (int k = 0; k < sc; ++k) px += b.on(x, y) ? "█" : " ";
                    if (!b.on(x, y)) { out += px; continue; }
                    double d = std::fabs(x + y * 1.2 - pos);
                    RGB c = d < 2.5 ? mix(target(b, x, y), {255, 250, 235}, 0.7 * (1 - d / 2.5)) : target(b, x, y);
                    out += ui.fgCode(c) + px;
                }
                out += "\x1b[0m\x1b[K\r\n";
            }
            writeFrame(out + syncOff());
            animSleep(28);
        }
    }

    // Goodbye line: the text cools down from white-hot to grey.
    static void farewell() {
        auto& ui = UI::get();
        std::string msg = "Горн погашен. До встречи.";
        if (!ui.rgbOk() || !isTTY()) { std::cout << ui.muted(msg) << "\n"; return; }
        static const char* fancy[] = {"✽", "✻", "✶", "✳", "✢", "·"};
        static const char* plain[] = {"*", "*", "*", "+", "+", "·"};
        const char** g = gl::rich ? fancy : plain;
        for (int f = 0; f <= 12; ++f) {
            double t = f / 12.0;
            RGB c = t < 0.5 ? mix(ui.theme.hot, ui.theme.accent, t * 2) : mix(ui.theme.accent, hex(0x6A6460), (t - 0.5) * 2);
            writeFrame(syncOn() + inPlace(ui.rgb(c, std::string(g[std::min(5, f / 2)]) + " " + msg)) + syncOff());
            animSleep(34);
        }
        std::cout << "\n";
    }

    // animate=false: boot checks + static logo (the forge animation is shown once in a while,
    // not on every start).
    static void run(const std::vector<Step>& steps, bool animate = true) {
        Term::raw(true); Term::hideCursor(true);
        bool full = boot(steps) && animate;
        std::cout << "\x1b[2J\x1b[H" << std::flush;
        if (full) forge(); else { std::cout << "\n\n\n"; printLogo(); }
        tagline(full);
        Term::drain();
        Term::raw(false); Term::hideCursor(false);
    }
};

} // namespace nova
