// NovaForge — AI assistant for coding and automation in the terminal.
// Copyright (c) 2026 NovaForge. MIT License.
#include "server.hpp"
#include "intro.hpp"
#include "editor.hpp"
#include <set>
#include <csignal>

using namespace nova;

namespace {

struct App {
    Session session;
    bool plan = false;             // read-only mode (Shift+Tab, /plan, --plan)
    bool autoApprove = false;
    std::set<std::string> alwaysAllow;
    Spinner spinner;
    MarkdownStream md;
    bool textStarted = false;
    int bashLines = 0;
    bool toMenu = false;
    fs::path launchDir;
    Json lastArgs;                 // arguments of the running tool (for diffs)
    std::string verb;              // spinner verb of the current turn
    int shownLines = 0, hiddenLines = 0;
} app;

std::atomic<bool> g_inTurn{false};

// Watches the keyboard while the model works: Esc stops the turn (Ctrl+C does the same via SIGINT).
class EscWatch {
    std::atomic<bool> on_{false}, paused_{false}, busy_{false}; std::thread th_;
public:
    void start() {
        if (!Term::interactive() || on_) return;
        Term::raw(true); on_ = true; paused_ = false;
        th_ = std::thread([this] {
            while (on_) {
                if (paused_) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); continue; }
                busy_ = true;
                if (paused_) { busy_ = false; continue; }
                if (Term::keyWaiting(40)) {
                    int c = Term::readByte(0);
                    if (c == 27) {
                        int c2 = Term::readByte(15);
                        if (c2 < 0) cancelFlag() = true;
                        else while (Term::keyWaiting(5)) Term::readByte(0);   // arrow keys etc. are ignored
                    }
                }
                busy_ = false;
            }
        });
    }
    // After pause() returns the watcher no longer reads the keyboard (prompts can use it).
    void pause() { paused_ = true; while (busy_) std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    void resume() { if (!on_) return; Term::raw(true); paused_ = false; }
    void stop() { if (!on_) return; on_ = false; if (th_.joinable()) th_.join(); Term::raw(false); }
} escWatch;

UI& ui() { return UI::get(); }
std::string providerStatus(const Provider& p, bool& connected);
std::vector<std::string> providerModels(const Provider& p);
std::string resolveModel(const std::string& arg);
std::string pickModel(const std::string& title, const std::string& current);
bool connectFlow(bool chooseModel);
void newConnection();
bool refreshModels(const Provider& p, std::vector<std::string>& ids, bool quiet = false);
std::string chooseFromModels(const Provider& p, const std::vector<std::string>& ids, const std::string& current);

Mode currentMode() { return app.plan ? Mode::plan() : Mode::normal(); }
std::string sessionModel() { return Engine::modelFor(app.session); }

std::string homeShort(const std::string& p) {
    std::string h = homeDir().string();
    if (!h.empty() && startsWith(p, h)) return "~" + p.substr(h.size());
    return p;
}

std::string firstLine(const std::string& s) { return trim(split(trim(s), '\n')[0]); }

std::string tokens(long long n) {
    char b[32];
    if (n >= 10000) snprintf(b, sizeof b, "%.0fk", n / 1000.0);
    else if (n >= 1000) snprintf(b, sizeof b, "%.1fk", n / 1000.0);
    else snprintf(b, sizeof b, "%lld", n);
    return b;
}

std::string toolArgSummary(const std::string& n, const Json& a) {
    if (n == "bash") return a.getString("command");
    if (n == "grep") return a.getString("pattern") + (a.getString("include").empty() ? "" : "  " + a.getString("include"));
    if (n == "glob") return a.getString("pattern");
    if (n == "webfetch") return a.getString("url");
    if (n == "todowrite") return std::to_string(a.get("todos").size()) + " задач";
    if (n == "todoread") return "";
    return a.getString("path", ".");
}


// ---------- conversation view ----------

std::string toolTitle(const std::string& n) {
    static const std::map<std::string, std::string> t = {
        {"read", "Read"}, {"write", "Write"}, {"edit", "Update"}, {"multiedit", "Update"}, {"list", "List"}, {"glob", "Glob"},
        {"grep", "Search"}, {"bash", "Bash"}, {"webfetch", "Fetch"}, {"todowrite", "Todo"}, {"todoread", "Todo"}};
    auto it = t.find(n); if (it != t.end()) return it->second;
    std::string x = n; if (!x.empty()) x[0] = (char)toupper((unsigned char)x[0]); return x;
}
std::string toolVerb(const std::string& n) {
    static const std::map<std::string, std::string> t = {
        {"read", "Читаю"}, {"write", "Пишу"}, {"edit", "Правлю"}, {"multiedit", "Правлю"}, {"list", "Осматриваю"}, {"glob", "Ищу файлы"},
        {"grep", "Ищу"}, {"bash", "Выполняю"}, {"webfetch", "Загружаю"}, {"todowrite", "Планирую"}, {"todoread", "Сверяюсь с планом"}};
    auto it = t.find(n); return it == t.end() ? "Работаю" : it->second;
}
std::string randomVerb() {
    static const std::vector<std::string> v = {"Раздуваю горн", "Кую", "Закаляю", "Плавлю", "Чеканю", "Отливаю", "Шлифую",
                                               "Разогреваю металл", "Обдумываю", "Высекаю искры", "Правлю на наковальне", "Куя, думаю"};
    static std::mt19937 rng{(unsigned)nowMs()};
    return v[rng() % v.size()];
}

void printToolHead(const std::string& n, const Json& a, int state) {   // state: 0 running, 1 ok, 2 failed
    std::string arg = toolArgSummary(n, a);
    if (n == "bash") arg = firstLine(arg);
    std::string dot = state == 0 ? ui().accent(gl::dot) : state == 1 ? ui().ok(gl::dot) : ui().err(gl::dot);
    std::string head = ui().bold(toolTitle(n)) + (arg.empty() ? "" : ui().muted("(") + arg + ui().muted(")"));
    std::cout << dot << " " << ui().fit(head, (size_t)UI::cols() - 4) << "\n";
}
void printResultLine(const std::string& s, bool first) {
    std::cout << (first ? "  " + ui().muted(gl::elbow) + "  " : "     ") << ui().fit(s, (size_t)UI::cols() - 7) << "\n";
}
void printDiff(const std::string& oldS, const std::string& newS) {
    auto a = split(oldS, '\n'), b = split(newS, '\n');
    size_t room = (size_t)UI::cols() - 9; int shown = 0;
    for (size_t i = 0; i < a.size() && shown < 8; ++i, ++shown) std::cout << "       " << ui().err("- " + ui().fit(a[i], room)) << "\n";
    for (size_t i = 0; i < b.size() && shown < 16; ++i, ++shown) std::cout << "       " << ui().ok("+ " + ui().fit(b[i], room)) << "\n";
    size_t total = a.size() + b.size();
    if (total > (size_t)shown) std::cout << "       " << ui().muted("… ещё " + std::to_string(total - (size_t)shown) + " строк") << "\n";
}

void printTodos(const Json& todos) {
    if (todos.size() == 0) { ui().note("Список задач пуст"); return; }
    bool first = true;
    for (auto& t : todos.arr()) {
        std::string st = t.getString("status", "pending"), c = t.getString("content");
        std::string line = st == "completed" ? ui().muted(std::string(gl::todoDone) + " " + c) : st == "in_progress" ? ui().bold(ui().accent(std::string(gl::todoActive) + " ") + c) : std::string(gl::todo) + " " + c;
        printResultLine(line, first); first = false;
    }
}

EngineEvents tuiEvents() {
    EngineEvents ev;
    ev.onStep = [](int step) {
        app.textStarted = false;
        app.spinner.start(step == 1 ? app.verb : randomVerb(), step > 1 ? "шаг " + std::to_string(step) : "");
    };
    ev.onText = [](const std::string& d) {
        if (!app.textStarted) { app.spinner.stop(); app.textStarted = true; std::cout << "\n"; app.md.bullet(); }
        app.md.feed(d);
    };
    ev.onResponseEnd = [] { app.spinner.stop(); app.md.flush(); };
    ev.onNotice = [](const std::string& n) {
        app.spinner.stop();
        std::cout << "\n" << ui().warn(gl::dot) << " " << ui().muted(n) << "\n";
    };
    ev.onToolStart = [](const std::string& n, const Json& a) {
        app.spinner.stop();
        if (app.textStarted) app.textStarted = false;
        app.lastArgs = a; app.shownLines = 0; app.hiddenLines = 0;
        if (n == "bash") {
            std::cout << "\n"; printToolHead(n, a, 0);
            app.spinner.start("Выполняю");
        } else {
            std::string arg = toolArgSummary(n, a);
            app.spinner.start(toolVerb(n) + (arg.empty() ? "" : " " + ui().fit(arg, 50, "…")));
        }
    };
    ev.onOutput = [](const std::string& line) {
        std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (app.shownLines >= 6) { ++app.hiddenLines; return; }
        bool running = app.spinner.running(); app.spinner.stop();
        printResultLine(ui().muted(l), app.shownLines == 0); ++app.shownLines;
        if (running) app.spinner.start("Выполняю");
    };
    ev.onToolEnd = [](const std::string& n, const ToolResult& r) {
        app.spinner.stop();
        const Json& a = app.lastArgs;
        if (n == "bash") {
            if (app.hiddenLines) printResultLine(ui().muted("… +" + std::to_string(app.hiddenLines) + " " + plural(app.hiddenLines, "строка", "строки", "строк")), false);
            std::string code = r.title.substr(r.title.rfind(" · ") == std::string::npos ? 0 : r.title.rfind(" · ") + 3 + 0);
            if (!r.ok) printResultLine(ui().err(r.output.find("[exit code") == std::string::npos && r.output.find("[останов") == std::string::npos ? firstLine(r.output) : code), app.shownLines == 0);
            else if (app.shownLines == 0) printResultLine(ui().muted("(нет вывода)"), true);
        } else {
            std::cout << "\n"; printToolHead(n, a, r.ok ? 1 : 2);
            if (!r.ok) printResultLine(ui().err(firstLine(r.output)), true);
            else if (n == "edit") { std::string t = r.title; printResultLine(t, true); printDiff(a.getString("old_string"), a.getString("new_string")); }
            else if (n == "multiedit") {
                printResultLine(r.title, true);
                int k = 0; for (auto& e : a.get("edits").arr()) { if (++k > 3) break; printDiff(e.getString("old_string"), e.getString("new_string")); }
            }
            else if (n == "write") {
                printResultLine(r.title, true);
                auto lines = split(a.getString("content"), '\n'); size_t room = (size_t)UI::cols() - 9;
                while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
                for (size_t i = 0; i < lines.size() && i < 6; ++i) std::cout << "       " << ui().ok(ui().fit(lines[i], room)) << "\n";
                if (lines.size() > 6) std::cout << "       " << ui().muted("… ещё " + std::to_string(lines.size() - 6) + " строк") << "\n";
            }
            else if (n == "todowrite" && app.session.todos.size()) printTodos(app.session.todos);
            else printResultLine(ui().muted(r.title.empty() ? firstLine(r.output) : r.title), true);
        }
        app.spinner.start(randomVerb());
    };
    ev.askPermission = [](const std::string& group, const std::string& title, const std::string& detail) -> bool {
        if (app.autoApprove || app.alwaysAllow.count(group)) return true;
        app.spinner.stop();
        if (!isStdinTTY()) {
            ui().warning(title + " — отклонено: нет терминала для подтверждения (запустите с --yes)");
            return false;
        }
        escWatch.pause();
        std::vector<std::string> lines = {ui().bold(group == "bash" ? "Команда shell" : group == "edit" ? "Изменение файла" : group == "webfetch" ? "Загрузка из сети" : group) + ui().muted("  " + title), ""};
        int shown = 0;
        for (auto& l : split(detail, '\n')) {
            if (trim(l).empty()) continue;
            if (++shown > 12) { lines.push_back(ui().muted("  …")); break; }
            lines.push_back("  " + l);
        }
        lines.push_back("");
        std::cout << "\n"; box(lines, ui().theme.warn);
        int r = menu("Разрешить?", {{"Да", ""}, {"Да, и не спрашивать про " + group + " до конца сессии", ""}, {"Нет, сказать, что сделать иначе", "  esc"}}, 0, "↑↓ выбрать   Enter — ответить   Esc — нет");
        bool ok = r == 0 || r == 1;
        if (r == 1) app.alwaysAllow.insert(group);
        if (!ok) {
            std::string fb = ask("Что сделать иначе (Enter — просто отказать)");
            permissionFeedback() = fb;
            printResultLine(ui().err("отклонено") + (fb.empty() ? "" : ui().muted(" · " + fb)), true);
        }
        escWatch.resume();
        return ok;
    };
    return ev;
}

// Expand @file mentions into attached file contents.
std::string expandMentions(const std::string& input) {
    std::string extra, tok;
    std::set<std::string> seen;
    auto take = [&]() {
        if (tok.size() >= 2 && tok[0] == '@') {
            std::string p = tok.substr(1);
            while (!p.empty() && std::string(",.:;)!?").find(p.back()) != std::string::npos) p.pop_back();
            std::error_code ec;
            if (!p.empty() && !seen.count(p) && fs::is_regular_file(p, ec)) {
                std::string c = readFile(p);
                if (!isBinaryContent(c)) {
                    seen.insert(p);
                    extra += "\n\n<file path=\"" + p + "\">\n" + truncateUtf8(c, 30000) + "\n</file>";
                    ui().note("прикреплён " + p);
                }
            }
        }
        tok.clear();
    };
    for (char c : input) { if (c == ' ' || c == '\n' || c == '\t') take(); else tok += c; }
    take();
    return input + extra;
}

bool runTurn(const std::string& text) {
    if (sessionModel().empty()) {
        ui().warning("модель не выбрана");
        if (!Term::interactive() || !connectFlow(true) || sessionModel().empty()) {
            ui().note("Подключите API: /connect (или nova connect) — модели загрузятся с сервера провайдера");
            return false;
        }
    }
    Mode mode = currentMode();
    long long t0 = nowMs();
    long long tin = app.session.tokensIn, tout = app.session.tokensOut;
    app.session.beginTurn();
    EngineEvents ev = tuiEvents();
    app.verb = randomVerb();
    cancelFlag() = false; g_inTurn = true;
    escWatch.start();
    auto r = Engine::run(app.session, mode, text, ev);
    escWatch.stop();
    g_inTurn = false;
    app.spinner.stop(); app.md.flush();
    app.session.endTurn();
    app.session.save();
    bool cancelled = r.cancelled || cancelFlag();
    cancelFlag() = false;
    if (cancelled) { std::cout << "\n"; printResultLine(ui().err("Прервано") + ui().muted(" · можно написать, что делать дальше"), true); }
    else if (!r.ok) { std::cout << "\n" << ui().err(gl::dot) << " " << ui().err("Ошибка") << "\n"; auto el = UI::wrap(r.error, (size_t)UI::cols() - 8); for (size_t i = 0; i < el.size(); ++i) printResultLine(el[i], i == 0); }
    std::string stats = fmtDuration(nowMs() - t0);
    if (r.steps > 1) stats += " · " + std::to_string(r.steps) + " " + plural(r.steps, "шаг", "шага", "шагов");
    long long din = app.session.tokensIn - tin, dout = app.session.tokensOut - tout;
    if (din || dout) stats += " · ↑ " + tokens(din) + " ↓ " + tokens(dout);
    std::cout << "\n" << ui().muted(std::string(gl::star) + " " + stats) << "\n";
    return r.ok;
}

std::string shortModel(const std::string& m) { return m.size() > 38 ? ui().fit(m, 38) : m; }

std::string instructionsFile() {
    for (const char* f : {"NOVA.md", "AGENTS.md", "CLAUDE.md"}) if (fs::exists(f)) return f;
    return "";
}

// ---------- project instructions ----------
// Marker of an untouched template: /init and the tips know the file still has to be filled in.
const char* kTemplateMark = "<!-- novaforge:template -->";

bool isUntouchedTemplate(const fs::path& f) { return readFile(f).find(kTemplateMark) != std::string::npos; }

std::string projectTemplate(const std::string& name) {
    return std::string(kTemplateMark) + "\n"
        "# " + name + "\n\n"
        "Инструкции проекта для NovaForge. Этот файл добавляется к каждому запросу к модели в этой папке:\n"
        "пишите сюда то, что ассистент должен всегда помнить. `/init` заполнит его по содержимому проекта.\n\n"
        "## О проекте\n"
        "- Что это: _(одно-два предложения: что делаем и для кого)_\n"
        "- Стек: _(языки, фреймворки, версии)_\n\n"
        "## Команды\n"
        "- Сборка: _(например `cmake --build build`)_\n"
        "- Тесты: _(например `ctest` или `npm test`)_\n"
        "- Запуск: _(как запустить и проверить результат)_\n\n"
        "## Правила для ассистента\n"
        "- Отвечай на русском, кратко.\n"
        "- Перед большими изменениями покажи план и дождись подтверждения.\n"
        "- После правок запускай сборку/тесты, если они указаны выше.\n"
        "- Не изменяй: _(папки и файлы, которые трогать нельзя)_\n\n"
        "## Заметки\n"
        "_(договорённости, решения, ссылки — всё, что полезно помнить между сессиями)_\n";
}

// Read-me in the projects folder (created once, never overwritten).
void ensureProjectsGuide(const fs::path& base) {
    fs::path f = base / fs::u8path("Инструкция.md");
    std::error_code ec; if (fs::exists(f, ec)) return;
    writeFile(f,
        "# Проекты NovaForge\n\n"
        "Каждая папка здесь — отдельный проект. Программа установлена отдельно, эта папка принадлежит вам:\n"
        "при удалении или обновлении NovaForge проекты не трогаются.\n\n"
        "## Как начать\n"
        "1. Запустите `nova` → «Создать новую сессию?» → Enter → введите название проекта.\n"
        "2. В новом проекте появится `NOVA.md` — инструкции для ассистента. Заполните его сами или командой `/init`.\n"
        "3. Пишите задачи обычным текстом. `@файл` — приложить файл, `!команда` — выполнить в терминале.\n\n"
        "## Полезные команды в чате\n"
        "| Команда | Что делает |\n|---|---|\n"
        "| `/init` | изучить проект и заполнить `NOVA.md` |\n"
        "| `/model` | выбрать модель |\n"
        "| `/plan` | режим только чтения: план без правок |\n"
        "| `/undo`, `/redo` | откатить или вернуть ход вместе с правками файлов |\n"
        "| `/menu` | главное меню (проекты, сессии, настройки) |\n"
        "| `/help` | все команды |\n\n"
        "## Где что лежит\n"
        "- Проекты — эта папка (меняется в «Настройки → Папка проектов»).\n"
        "- Настройки, ключи API и история сессий — `%APPDATA%\\NovaForge` в Windows, `~/.novaforge` в Linux/macOS.\n"
        "- Общие инструкции для всех проектов — `NOVA.md` в папке настроек (`/instructions global`).\n\n"
        "## Инструкции для ассистента\n"
        "| Что | Где | Как открыть |\n|---|---|---|\n"
        "| Правила проекта | `NOVA.md` в папке проекта | `/instructions edit` или `/init` |\n"
        "| Общие правила (все проекты) | `NOVA.md` в папке настроек | `/instructions global`, «Настройки → Общие инструкции» |\n"
        "| Дополнительные файлы | `\"instructions\": [\"путь\"]` в config.json | `nova config set instructions '[\"C:/docs/style.md\"]'` |\n\n"
        "`/instructions` показывает, какие файлы ассистент читает сейчас. Также читаются `AGENTS.md` и `CLAUDE.md`.\n");
}

// ---------- instructions: where they live and how to edit them ----------
fs::path globalInstructionsFile() {
    fs::path n = novaHome() / "NOVA.md";
    if (!fs::exists(n) && fs::exists(novaHome() / "AGENTS.md")) return novaHome() / "AGENTS.md";
    return n;
}

std::string globalTemplate() {
    return "# Общие инструкции NovaForge\n\n"
        "Этот файл добавляется к запросам во ВСЕХ проектах. Здесь — ваши личные предпочтения;\n"
        "правила конкретного проекта пишите в NOVA.md в папке проекта.\n\n"
        "## Обо мне\n"
        "- _(кто вы, какой опыт, ОС и редактор)_\n\n"
        "## Как отвечать\n"
        "- Отвечай на русском, кратко и по делу.\n"
        "- Код и комментарии в коде — на английском.\n";
}

// Opens a text file in the user's editor (Notepad on Windows), creating it from a template if needed.
void openInEditor(const fs::path& f, const std::string& templ) {
    std::error_code ec;
    if (!fs::exists(f, ec)) writeFile(f, templ);
#ifdef _WIN32
    std::wstring cmd = L"start \"\" notepad.exe \"" + f.wstring() + L"\"";
    _wsystem(cmd.c_str());
#else
    std::string q = "'" + replaceAll(f.string(), "'", "'\\''") + "'";
    std::string ed = env("VISUAL", env("EDITOR"));
    if (!ed.empty()) std::system((ed + " " + q).c_str());
    else std::system(("xdg-open " + q + " >/dev/null 2>&1 || open " + q + " >/dev/null 2>&1").c_str());
#endif
    ui().success("Открыт " + homeShort(f.string()));
}

// /instructions: what the assistant reads, in which order.
void showInstructions() {
    std::cout << "\n" << ui().bold(" Инструкции, которые читает ассистент") << "\n\n";
    auto row = [](const std::string& what, const fs::path& f, const std::string& hint) {
        std::error_code ec; bool ok = fs::exists(f, ec);
        std::string state;
        if (ok) { size_t n = 0; for (char ch : readFile(f)) n += ch == '\n'; state = std::to_string(n) + " " + plural((long long)n, "строка", "строки", "строк") + (isUntouchedTemplate(f) ? " · шаблон, не заполнен" : ""); }
        std::cout << "  " << (ok ? ui().ok("●") : ui().muted("○")) << " " << UI::pad(what, 10) << homeShort(f.string()) << "\n"
                  << "    " << ui().muted(ok ? state : "нет — " + hint) << "\n";
    };
    std::string pf = instructionsFile();
    row("проект", fs::current_path() / (pf.empty() ? "NOVA.md" : pf), "/instructions edit или /init");
    row("общие", globalInstructionsFile(), "/instructions global");
    for (auto& i : Config::get().data.get("instructions").arr())
        if (i.isString()) row("доп.", fs::path(i.str()), "файл не найден (config: instructions)");
    std::cout << "\n" << ui().muted("  /instructions edit — инструкции проекта · /instructions global — общие для всех проектов\n"
                                     "  /init — заполнить NOVA.md по содержимому проекта") << "\n";
}

void printHeader() {
    std::string model = sessionModel();
    std::vector<std::string> lines = {
        ui().accent(gl::star) + " " + ui().bold("Добро пожаловать в NovaForge") + ui().muted("  v" + std::string(VERSION)),
        "",
        ui().muted("  /help — команды · /menu — главное меню · ? — горячие клавиши"),
        "",
        ui().muted("  проект  ") + homeShort(fs::current_path().string()),
        ui().muted("  модель  ") + (model.empty() ? ui().warn("не выбрана") + ui().muted(" — /connect") : model),
    };
    int W = 58; for (auto& l : lines) W = std::max(W, (int)UI::width(l) + 4);
    W = std::min(UI::cols() - 1, W);
    box(lines, ui().theme.accent, W);
    std::cout << "\n" << ui().muted(" Для начала:") << "\n";
    std::vector<std::string> tips = {"Спросите о проекте или опишите задачу", "@файл — приложить файл, !команда — выполнить в shell",
                                     "Shift+Tab — режим: обычный, авто-подтверждение, plan (только чтение)"};
    if (model.empty()) tips.insert(tips.begin(), "/connect — подключите API: модели загрузятся с сервера провайдера");
    else if (instructionsFile().empty()) tips.insert(tips.begin() + 1, "/init — изучить проект и записать заметки в NOVA.md");
    else if (isUntouchedTemplate(instructionsFile())) tips.insert(tips.begin() + 1, "NOVA.md — инструкции проекта: заполните его или выполните /init");
    for (size_t i = 0; i < tips.size(); ++i) std::cout << ui().muted(" " + std::to_string(i + 1) + ". " + tips[i]) << "\n";
}

void printHelp() {
    struct C { const char* cmd; const char* desc; };
    static const std::vector<std::pair<std::string, std::vector<C>>> groups = {
        {"Сессия", {{"/new", "новая сессия"}, {"/sessions", "список сессий"}, {"/resume [n|id]", "продолжить сессию"},
                    {"/undo, /redo", "откатить / вернуть последний ход и изменения файлов"}, {"/compact", "сжать историю"},
                    {"/export [файл]", "сохранить сессию в Markdown"}, {"/cost", "расход токенов"}, {"/todos", "список задач"}}},
        {"Модели и режимы", {{"/connect", "подключения API: ключ, загрузка моделей, локальные серверы"},
                    {"/models [refresh]", "модели подключённых API"}, {"/model [id]", "сменить модель (без id — выбор с поиском)"},
                    {"/plan", "режим plan: только чтение и план (вкл/выкл)"}, {"/details", "инструменты и разрешения"}}},
        {"Прочее", {{"/menu", "главное меню"}, {"/project", "текущая папка проекта"}, {"/init", "создать или обновить NOVA.md"}, {"/instructions", "где инструкции: edit — проекта, global — общие"}, {"/workflows", "воркфлоу"}, {"/run <wf> [ввод]", "запустить воркфлоу"},
                    {"/yolo", "вкл/выкл подтверждения действий"}, {"/theme [имя]", "цветовая тема"}, {"/clear", "очистить экран"}, {"/exit", "выход"}}},
        {"Ввод", {{"@файл", "приложить файл к сообщению"}, {"!команда", "выполнить команду, вывод попадёт в контекст"},
                    {"строка \\", "продолжить ввод на следующей строке"}}},
        {"Клавиши", {{"Tab", "дополнить команду или файл"}, {"↑ ↓", "история ввода"}, {"Alt+Enter, Ctrl+J", "новая строка"},
                    {"Shift+Tab", "режим: обычный → авто-подтверждение → plan"}, {"Esc", "прервать ответ"}, {"Esc Esc", "очистить ввод"},
                    {"Ctrl+L", "очистить экран"}, {"Ctrl+C дважды", "выход"}}},
    };
    for (auto& g : groups) {
        std::vector<std::string> lines;
        for (auto& c : g.second) lines.push_back(UI::pad(c.cmd, 24) + ui().muted(c.desc));
        ui().section(g.first, lines);
    }
}

void listModels(bool refresh) {
    std::string cur = sessionModel();
    std::vector<std::string> lines;
    for (auto& p : Config::get().providers()) {
        bool conn; std::string st = providerStatus(p, conn);
        if (!conn) continue;
        auto ids = providerModels(p);
        if (!(p.local() && !p.needsKey) && (refresh || ids.empty())) {
            std::vector<std::string> fresh;
            if (refreshModels(p, fresh, true)) ids = fresh;
        }
        if (!lines.empty()) lines.push_back("");
        bool local = p.local() && !p.needsKey;
        lines.push_back(ui().bold(p.name) + "  " + ui().ok(st) + (local ? "" : ui().muted("  " + std::to_string(ids.size()) + " " + plural((long long)ids.size(), "модель", "модели", "моделей"))));
        if (ids.empty()) lines.push_back(ui().muted("  список не загружен — проверьте ключ: /connect → " + p.name));
        size_t shown = 0;
        for (auto& m : ids) {
            std::string id = p.id + "/" + m;
            if (id != cur && shown >= 12) continue;
            lines.push_back(id == cur ? ui().accent("* " + id) : "  " + id); ++shown;
        }
        if (ids.size() > shown) lines.push_back(ui().muted("  … ещё " + std::to_string(ids.size() - shown) + " — полный список с поиском: /model"));
    }
    if (lines.empty()) { ui().note("Нет подключённых API и запущенных локальных серверов. Подключить: /connect"); return; }
    ui().section("Модели", lines);
    std::cout << "\n  " << ui().muted("Сменить: /model <провайдер/модель> или /model — выбор с поиском · обновить списки: /models refresh") << "\n";
}

void listSessions() {
    auto ss = Session::list(20);
    if (ss.empty()) { ui().note("Сессий пока нет"); return; }
    std::vector<std::string> lines; int i = 1;
    for (auto& s : ss) {
        std::string num = UI::pad(std::to_string(i++) + ".", 4);
        lines.push_back(ui().muted(num) + UI::pad(ui().fit(s.title, 44), 46) + ui().muted(isoTime(s.updated).substr(5, 11) + "  " + s.id));
    }
    ui().section("Сессии", lines);
    std::cout << "\n" << ui().muted("Продолжить: /resume <номер>") << "\n";
}

bool authLogin(std::string provider) {
    auto provs = Config::get().providers();
    if (provider.empty()) {
        std::vector<std::string> names;
        for (auto& p : provs) names.push_back(UI::pad(p.name, 22) + ui().muted(p.id));
        provider = provs[(size_t)choose("Провайдер", names)].id;
    }
    Provider p; if (!Config::get().findProvider(provider, p)) { ui().error("неизвестный провайдер " + provider); return false; }
    if (!p.needsKey) { ui().success(p.name + " не требует ключа. Проверьте, что сервер запущен: " + p.baseURL); return true; }
    std::string key = ask("API-ключ " + p.name);
    if (key.empty()) { ui().warning("ключ не введён"); return false; }
    Config::get().setKey(p.id, key);
    ui().success("Ключ сохранён в " + homeShort(Config::get().authPath.string()));
    std::vector<std::string> ids;
    if (refreshModels(p, ids) && Config::get().model().empty() && Term::interactive()) {
        std::string m = chooseFromModels(p, ids, "");
        if (!m.empty()) { Config::get().saveGlobal("model", m); ui().success("Модель по умолчанию: " + m); }
    }
    return true;
}

// ---------- screens: connections, models, themes ----------

std::string providerStatus(const Provider& p, bool& connected) {
    if (!p.needsKey && p.local()) {
        auto info = LLM::localInfo(p);
        connected = info.up && !info.models.empty();
        return info.detail;
    }
    std::string src = Config::get().auth().has(p.id) ? "ключ сохранён" : (!p.envKey.empty() && !env(p.envKey.c_str()).empty()) ? "$" + p.envKey : "";
    connected = !p.needsKey || !src.empty();
    if (!p.needsKey) return "без ключа";
    return src.empty() ? "не подключено" : src;
}

// Models of a provider: what a local server has installed, otherwise the list loaded from the API.
std::vector<std::string> providerModels(const Provider& p) {
    if (p.local() && !p.needsKey) return LLM::localInfo(p).models;
    return p.models;
}

// Local servers that are running and have at least one model.
std::vector<std::pair<Provider, std::vector<std::string>>> localServers() {
    std::vector<std::pair<Provider, std::vector<std::string>>> v;
    for (auto& p : Config::get().providers()) {
        if (p.needsKey || !p.local()) continue;
        auto info = LLM::localInfo(p);
        if (info.up && !info.models.empty()) v.push_back({p, info.models});
    }
    return v;
}

// "provider/model" as is; "gpt-4.1" or "qwen2.5-coder" → the connected provider that has it. "" if unknown.
std::string resolveModel(const std::string& arg) {
    if (arg.find('/') != std::string::npos) { Provider p; if (Config::get().findProvider(Config::splitModel(arg).first, p)) return arg; }
    for (auto& [p, models] : localServers())
        for (auto& m : models) if (m == arg || m == arg + ":latest" || startsWith(m, arg + ":")) return p.id + "/" + m;
    for (auto& p : Config::get().providers()) {
        bool conn; providerStatus(p, conn); if (!conn) continue;
        for (auto& m : p.models) if (m == arg || lower(m) == lower(arg)) return p.id + "/" + m;
    }
    return "";
}

bool fullScreen() { return ui().color && Term::interactive(); }
void screenTitle(const std::string& t) {
    if (fullScreen()) { ui().clearScreen(); std::cout << "\n"; }
    std::cout << "\n  " << ui().gradient("NOVAFORGE", ui().headGrad(), true) << ui().muted("  /  ") << ui().heading(t) << "\n";
    ui().replay();
}

bool enterKey(const Provider& p) {
    std::string key = ask("API-ключ " + p.name + (p.envKey.empty() ? "" : " (или переменная " + p.envKey + ")"));
    if (key.empty()) { ui().note("Ключ не введён"); return false; }
    Config::get().setKey(p.id, key);
    ui().success("Ключ для " + p.name + " сохранён");
    return true;
}

// Loads the model list from the provider and caches it in config.json.
bool refreshModels(const Provider& p, std::vector<std::string>& ids, bool quiet) {
    Spinner sp; sp.start("загружаю список моделей " + p.name);
    std::string detail; int code = LLM::fetchModels(p, ids, detail); sp.stop();
    bool ok = code >= 200 && code < 300 && !ids.empty();
    if (ok) {
        if (!(p.local() && !p.needsKey)) Config::get().saveModels(p.id, ids);
        if (!quiet) ui().success(p.name + ": " + detail);
    } else {
        ui().error(p.name + ": " + detail);
        if (p.local() && !p.needsKey) for (auto& l : UI::wrap(LLM::startHint(p), (size_t)UI::cols() - 6)) ui().note(l);
    }
    return ok;
}

// Model list with search. Returns "provider/model", "" if cancelled.
std::string chooseFromModels(const Provider& p, const std::vector<std::string>& ids, const std::string& current) {
    auto [curProv, curModel] = Config::splitModel(current);
    std::vector<MenuItem> ms; int def = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        ms.push_back({ids[i], ""});
        if (p.id == curProv && ids[i] == curModel) { def = (int)i; ms.back().hint = ui().muted("  текущая"); }
    }
    ms.push_back({"", "", true});
    ms.push_back({"Обновить список", "  загрузить заново с " + urlHost(p.baseURL)});
    ms.push_back({"Ввести название вручную…", ""});
    int r = menu("Модель · " + p.name, ms, def, "", ids.size() > 8);
    if (r < 0) return "";
    if ((size_t)r < ids.size()) return p.id + "/" + ids[(size_t)r];
    if (r == (int)ms.size() - 2) {
        std::vector<std::string> fresh;
        if (refreshModels(p, fresh) && !fresh.empty()) return chooseFromModels(p, fresh, current);
        return "";
    }
    std::string m = ask("Название модели");
    return m.empty() ? "" : p.id + "/" + m;
}

// Pick "provider/model" among connected APIs and running local servers; offers to connect a new API.
// Returns "-" if cancelled.
std::string pickModel(const std::string& title, const std::string& current) {
    while (true) {
        std::vector<Provider> conn; std::vector<MenuItem> items; int def = 0;
        auto curProv = Config::splitModel(current).first;
        for (auto& p : Config::get().providers()) {
            bool c; std::string st = providerStatus(p, c);
            if (!c) continue;
            auto ms = providerModels(p);
            std::string hint = ms.empty() ? "список моделей не загружен" : std::to_string(ms.size()) + " " + plural((long long)ms.size(), "модель", "модели", "моделей");
            if (p.id == curProv) def = (int)conn.size();
            conn.push_back(p);
            items.push_back({p.name, "  " + ui().ok(std::string(gl::dot) + " ") + hint});
        }
        if (!conn.empty()) items.push_back({"", "", true});
        items.push_back({"Подключить API…", "  OpenAI, Anthropic, OpenRouter, Gemini, свой адрес"});
        screenTitle(title);
        if (conn.empty()) { ui().note("Пока ничего не подключено. Выберите API и введите ключ — список моделей загрузится с сервера."); std::cout << "\n"; }
        int r = menu("Подключение", items, def);
        if (r < 0) return "-";
        if ((size_t)r >= conn.size()) { if (!connectFlow(false)) continue; continue; }
        Provider p = conn[(size_t)r];
        std::vector<std::string> ids = providerModels(p);
        screenTitle(title + " · " + p.name);
        if (ids.empty() && !refreshModels(p, ids, true)) { ui().note("Модель можно ввести вручную"); }
        std::string m = chooseFromModels(p, ids, current);
        if (!m.empty()) return m;
    }
}

// Choose an API, enter its key (or a custom address), load its models. With chooseModel the
// picked model becomes the session model (and the default one if none is set yet).
bool connectFlow(bool chooseModel) {
    if (chooseModel) {
        std::string m = pickModel("Модель", sessionModel());
        if (m == "-" || m.empty()) return false;
        app.session.model = m;
        if (Config::get().model().empty()) Config::get().saveGlobal("model", m);
        ui().success("Модель: " + m);
        return true;
    }
    auto provs = Config::get().providers();
    std::vector<MenuItem> items; std::vector<std::string> ids;
    for (auto& p : provs) {
        if (!p.needsKey && p.local()) continue;            // local servers need no connection
        bool c; std::string st = providerStatus(p, c);
        items.push_back({p.name, "  " + (c ? ui().ok(std::string(gl::dot) + " " + st) : ui().muted(urlHost(p.baseURL)))});
        ids.push_back(p.id);
    }
    items.push_back({"", "", true});
    items.push_back({"Свой адрес…", "  любой OpenAI-совместимый API"});
    screenTitle("Подключить API");
    int r = menu("", items, 0);
    if (r < 0) return false;
    if ((size_t)r >= ids.size()) { newConnection(); return true; }
    Provider p; Config::get().findProvider(ids[(size_t)r], p);
    bool c; providerStatus(p, c);
    if (!c && !enterKey(p)) return false;
    std::vector<std::string> models; refreshModels(p, models);
    return true;
}

void newConnection() {
    screenTitle("Новое подключение");
    ui().note("Любой OpenAI-совместимый API: свой сервер, прокси, vLLM, LiteLLM, второй аккаунт OpenAI и т. д.");
    ui().note("Ollama, LM Studio и llama.cpp находятся автоматически — для них ничего добавлять не нужно.");
    std::string id = lower(ask("Короткое имя (латиница), например work-openai"));
    if (!safeName(id) || id.find('.') != std::string::npos) { ui().error("некорректное имя: латиница, цифры, - и _"); return; }
    Provider existing;
    if (Config::get().findProvider(id, existing) && !confirm("Подключение " + id + " уже есть. Изменить?", false)) return;
    std::string name = ask("Название", id);
    std::string url = ask("Адрес API (base URL, обычно заканчивается на /v1)");
    if (!startsWith(url, "http://") && !startsWith(url, "https://")) { ui().error("адрес должен начинаться с http:// или https://"); return; }
    bool needsKey = confirm("Нужен API-ключ?", !isLocalHost(urlHost(url)));
    Config::get().saveProvider(id, name, url, {}, needsKey);
    Provider p; Config::get().findProvider(id, p);
    if (needsKey && !enterKey(p)) { ui().note("Ключ можно ввести позже: /connect → " + name); }
    std::vector<std::string> ids;
    if (!refreshModels(p, ids)) ui().note("Модель можно ввести вручную при выборе модели.");
    ui().success("Подключение " + ui().bold(id) + " сохранено");
}

void connectionActions(const std::string& id) {
    while (true) {
        Provider p; if (!Config::get().findProvider(id, p)) return;
        bool conn; std::string st = providerStatus(p, conn);
        bool custom = !Config::get().isBuiltinProvider(p.id);
        bool local = p.local() && !p.needsKey;
        screenTitle(p.name);
        auto pm = providerModels(p);
        std::string upd = Config::get().data.get("providers").get(p.id).getString("models_updated");
        ui().keyValues("", {{"имя", p.id}, {"адрес", p.baseURL}, {"статус", conn ? ui().ok(st) : ui().muted(st)},
                            {"модели", pm.empty() ? ui().muted(local ? "—" : "не загружены") : std::to_string(pm.size()) + (upd.empty() ? "" : ui().muted("  обновлено " + upd.substr(0, 10)))}});
        if (local && !conn) { std::cout << "\n"; for (auto& l : UI::wrap(LLM::startHint(p), (size_t)UI::cols() - 6)) ui().note(l); }
        std::cout << "\n";
        std::vector<MenuItem> items = {
            {p.needsKey ? (Config::get().auth().has(p.id) ? "Заменить ключ" : "Ввести ключ") : "Ключ не нужен", ""},
            {local ? "Проверить сервер" : "Обновить список моделей", local ? "" : "  с " + urlHost(p.baseURL)},
            {"Выбрать модель по умолчанию…", ""},
            {"Изменить адрес", ""},
            {"Удалить ключ", ""},
            {custom ? "Удалить подключение" : "Сбросить настройки", ""},
            {"", "", true},
            {"Назад", ""},
        };
        items[0].disabled = !p.needsKey;
        items[2].disabled = !conn;
        items[4].disabled = !Config::get().auth().has(p.id);
        int r = menu("", items, conn ? 2 : p.needsKey ? 0 : 1);
        if (r < 0 || r == 7) return;
        if (r == 0) { if (enterKey(p)) { Config::get().findProvider(id, p); std::vector<std::string> ids; refreshModels(p, ids); } }
        else if (r == 1) { std::vector<std::string> ids; refreshModels(p, ids); }
        else if (r == 2) {
            std::vector<std::string> ids = pm;
            if (ids.empty()) refreshModels(p, ids, true);
            std::string m = chooseFromModels(p, ids, Config::get().model());
            if (!m.empty()) { Config::get().saveGlobal("model", m); app.session.model = m; ui().success("Модель по умолчанию: " + m); }
        }
        else if (r == 3) {
            std::string url = ask("Адрес API", p.baseURL);
            if (!startsWith(url, "http://") && !startsWith(url, "https://")) ui().error("адрес должен начинаться с http:// или https://");
            else {
                Config::get().saveProvider(p.id, custom ? p.name : "", url, {}, p.needsKey);
                ui().success("Адрес обновлён");
                Config::get().findProvider(id, p);
                if (!p.needsKey || !Config::get().apiKey(p).empty()) { std::vector<std::string> ids; refreshModels(p, ids); }
            }
        }
        else if (r == 4) { Config::get().removeKey(p.id); ui().success("Ключ удалён"); }
        else if (r == 5) {
            if (!confirm(custom ? "Удалить подключение " + p.id + "?" : "Сбросить адрес и список моделей " + p.name + "?", false)) continue;
            if (custom) { Config::get().removeProvider(p.id); ui().success("Подключение удалено"); return; }
            Json g = Config::readJson(Config::get().globalPath);
            if (g["providers"].isObject()) g["providers"].erase(p.id);
            writeFile(Config::get().globalPath, g.dump(2)); Config::get().data["providers"].erase(p.id);
            ui().success("Настройки сброшены");
        }
    }
}

void connectionsMenu() {
    int def = 0;
    while (true) {
        auto provs = Config::get().providers();
        screenTitle("Подключения API");
        std::vector<MenuItem> items;
        for (auto& p : provs) {
            bool conn; std::string st = providerStatus(p, conn);
            std::string models;
            if (conn && !(p.local() && !p.needsKey)) models = ui().muted(p.models.empty() ? " · модели не загружены" : " · " + std::to_string(p.models.size()) + " " + plural((long long)p.models.size(), "модель", "модели", "моделей"));
            items.push_back({p.name, "  " + (conn ? ui().ok(std::string(gl::dot) + " ") : ui().muted("○ ")) + st + models});
        }
        items.push_back({"", "", true});
        items.push_back({"Новое подключение…", "  свой адрес и ключ"});
        items.push_back({"Назад", ""});
        int r = menu("", items, def);
        if (r < 0 || r == (int)items.size() - 1) return;
        def = r;
        if (r == (int)items.size() - 2) { newConnection(); continue; }
        connectionActions(provs[(size_t)r].id);
    }
}

void themeMenu() {
    std::vector<MenuItem> items; int def = 0;
    const auto& ts = themes();
    Theme saved = ui().theme; std::string savedName = ui().themeName;
    for (size_t i = 0; i < ts.size(); ++i) {
        ui().setTheme(ts[i].name);
        std::string sample = ts[i].ansi ? (ts[i].mono ? "" : ui().accent("████") + ui().ok("██") + ui().warn("██") + ui().err("██")) : ui().gradient("████████████", false);
        items.push_back({ts[i].name, "  " + sample + "  " + ui().muted(ts[i].title)});
        if (ts[i].name == savedName) def = (int)i;
    }
    ui().theme = saved; ui().themeName = savedName;
    int r = menu("Цветовая тема", items, def);
    if (r < 0) return;
    ui().setTheme(ts[(size_t)r].name);
    Config::get().saveGlobal("theme", ts[(size_t)r].name);
    ui().success("Тема " + ui().gradient(ts[(size_t)r].name));
}

void listWorkflows() {
    auto wfs = Workflows::list();
    if (wfs.empty()) { ui().note("Воркфлоу нет. Создать: nova workflow create <имя>"); return; }
    size_t nw = 0; for (auto& w : wfs) nw = std::max(nw, UI::width(w.first));
    std::vector<std::string> lines;
    for (auto& [n, w] : wfs) {
        const Json& tr = w.get("trigger"); std::string t = tr.getString("type", "manual");
        std::string trig = t == "interval" ? "каждые " + fmtDuration((long long)tr.getNum("seconds", 3600) * 1000) : t == "watch" ? "при изменении файлов" : "вручную";
        size_t steps = w.get("steps").size();
        lines.push_back(ui().bold(UI::pad(n, nw + 2)) + ui().muted(UI::pad(std::to_string(steps) + " " + plural((long long)steps, "шаг", "шага", "шагов") + ", " + trig, 30)) + w.getString("description"));
    }
    ui().section("Воркфлоу", lines);
}

bool runWorkflow(const std::string& name, const std::string& input) {
    if (name.empty()) { ui().error("укажите имя воркфлоу"); listWorkflows(); return false; }
    Json wf; if (!Workflows::find(name, wf)) { ui().error("воркфлоу не найден: " + name); return false; }
    std::cout << "\n" << ui().bold(name) << ui().muted("  " + wf.getString("description")) << "\n";
    auto r = Workflows::run(wf, input, app.autoApprove, [](const std::string& l) {
        if (startsWith(l, "  ok")) std::cout << "  " << ui().ok(l.substr(2)) << "\n";
        else if (startsWith(l, "  fail")) std::cout << "  " << ui().err(l.substr(2)) << "\n";
        else if (startsWith(l, "skip")) std::cout << "  " << ui().muted(l) << "\n";
        else if (startsWith(l, "    ")) std::cout << "  " << ui().muted(l) << "\n";
        else std::cout << "  " << l << "\n";
    });
    std::cout << "\n";
    if (r.ok) {
        ui().success("Готово за " + fmtDuration(r.ms));
        if (!trim(r.output).empty()) { MarkdownStream m; m.feed(r.output + "\n"); m.flush(); }
    } else ui().error(r.error);
    return r.ok;
}

bool doInit() {
    std::string f = instructionsFile(); if (f.empty()) f = "NOVA.md";
    std::string prompt = "Проанализируй этот проект (структура, языки, сборка, тесты, стиль кода, ключевые модули) и создай файл " + f +
                         " в корне: краткое описание, команды сборки/тестов/линтера, архитектура, соглашения по коду. "
                         "Если " + f + " уже есть — улучши его.";
    if (fs::exists(f) && isUntouchedTemplate(f))
        prompt += " Сейчас " + f + " — незаполненный шаблон: замени заготовки в скобках _( … )_ реальными данными проекта, "
                  "сохрани раздел «Правила для ассистента» и удали строку " + std::string(kTemplateMark) + ".";
    bool plan = app.plan; app.plan = false;
    bool ok = runTurn(prompt);
    app.plan = plan;
    return ok;
}

bool handleSlash(const std::string& line) {
    auto sp = line.find(' ');
    std::string cmd = line.substr(0, sp), arg = sp == std::string::npos ? "" : trim(line.substr(sp + 1));
    if (cmd == "/exit" || cmd == "/quit" || cmd == "/q") return false;
    else if (cmd == "/menu") { app.toMenu = true; return false; }
    else if (cmd == "/project") ui().info("Проект: " + homeShort(fs::current_path().string()));
    else if (cmd == "/instructions" || cmd == "/instr") {
        if (arg == "edit" || arg == "project") {
            std::string f = instructionsFile();
            openInEditor(fs::current_path() / (f.empty() ? "NOVA.md" : f), projectTemplate(fs::current_path().filename().string()));
        }
        else if (arg == "global") openInEditor(globalInstructionsFile(), globalTemplate());
        else showInstructions();
    }
    else if (cmd == "/connect" || cmd == "/connections") { connectionsMenu(); ui().clearScreen(); printHeader(); }
    else if (cmd == "/help" || cmd == "/?") printHelp();
    else if (cmd == "/clear") { if (ui().color) std::cout << "\x1b[2J\x1b[H"; printHeader(); }
    else if (cmd == "/new") { app.session = Session::create(app.session.model); app.alwaysAllow.clear(); ui().success("Новая сессия " + ui().muted(app.session.id)); }
    else if (cmd == "/sessions") listSessions();
    else if (cmd == "/resume" || cmd == "/continue") {
        auto ss = Session::list(20); Session s; bool ok = false;
        if (arg.empty() && !ss.empty()) { s = ss[0]; ok = true; }
        else if (!arg.empty() && std::all_of(arg.begin(), arg.end(), [](char c) { return isdigit((unsigned char)c); })) {
            size_t n = std::stoul(arg); if (n >= 1 && n <= ss.size()) { s = ss[n - 1]; ok = true; }
        }
        if (ok) ok = Session::load(s.id, s);
        else if (!arg.empty()) ok = Session::load(arg, s);
        if (!ok) ui().error("сессия не найдена" + (arg.empty() ? std::string() : ": " + arg));
        else {
            app.session = s; app.alwaysAllow.clear();
            ui().success("Продолжаем «" + s.title + "»" + ui().muted("  " + countOf((long long)s.messages.size(), "сообщение", "сообщения", "сообщений")));
        }
    }
    else if (cmd == "/plan") {
        app.plan = !app.plan;
        if (app.plan) ui().success("Режим plan: только чтение и план, файлы не меняются");
        else ui().success("Обычный режим");
    }
    else if (cmd == "/models") listModels(arg == "refresh" || arg == "--refresh" || arg == "обновить");
    else if (cmd == "/model") {
        std::string m = arg.empty() ? pickModel("Модель сессии", sessionModel()) : resolveModel(arg);
        if (arg.empty()) { ui().clearScreen(); printHeader(); }
        if (m == "-") return true;
        if (m.empty()) { ui().error("модель «" + arg + "» не найдена среди загруженных. Укажите провайдер/модель или выберите: /model"); return true; }
        app.session.model = m; ui().success("Модель " + m);
        Provider p; bool conn = true; if (Config::get().findProvider(Config::splitModel(m).first, p)) providerStatus(p, conn);
        if (!conn) ui().warning(p.local() ? p.name + " сейчас недоступен: " + LLM::startHint(p) : "у " + p.name + " нет ключа — /connect");
        if (Config::get().model() != m && confirm("Сохранить как модель по умолчанию?", Config::get().model().empty())) Config::get().saveGlobal("model", m);
    }
    else if (cmd == "/undo") {
        int n = app.session.undo();
        if (n < 0) ui().note("Нечего откатывать");
        else { app.session.save(); ui().success("Последний ход отменён, восстановлено файлов: " + std::to_string(n)); }
    }
    else if (cmd == "/redo") {
        int n = app.session.redo();
        if (n < 0) ui().note("Нечего возвращать");
        else { app.session.save(); ui().success("Ход возвращён, файлов: " + std::to_string(n)); }
    }
    else if (cmd == "/compact" || cmd == "/summarize") {
        Spinner s; s.start("сжимаю историю"); std::string err; bool ok = Engine::compact(app.session, err); s.stop();
        if (ok) { app.session.save(); ui().success("История сжата"); } else ui().error(err);
    }
    else if (cmd == "/todos") printTodos(app.session.todos);
    else if (cmd == "/export") {
        std::string f = arg.empty() ? "novaforge-" + app.session.id + ".md" : arg;
        if (writeFile(f, app.session.exportMarkdown())) ui().success("Сохранено в " + f); else ui().error("не удалось записать " + f);
    }
    else if (cmd == "/init") doInit();
    else if (cmd == "/theme" || cmd == "/themes") {
        if (arg.empty()) { themeMenu(); return true; }
        if (ui().setTheme(arg)) { Config::get().saveGlobal("theme", arg); ui().success("Тема " + arg); }
        else ui().error("нет темы " + arg + " (доступны: " + joinStr(UI::themeNames(), ", ") + ")");
    }
    else if (cmd == "/yolo") {
        app.autoApprove = !app.autoApprove;
        if (app.autoApprove) ui().warning("подтверждения выключены — файлы правятся и команды запускаются без вопросов");
        else ui().success("Подтверждения включены");
    }
    else if (cmd == "/cost" || cmd == "/tokens") {
        ui().keyValues("Сессия", {
            {"id", app.session.id}, {"сообщений", std::to_string(app.session.messages.size())},
            {"входящие", std::to_string(app.session.tokensIn) + " токенов"}, {"исходящие", std::to_string(app.session.tokensOut) + " токенов"}});
    }
    else if (cmd == "/details") {
        std::vector<std::string> lines;
        Mode a = currentMode();
        for (auto& t : ToolRegistry::get().all()) {
            if (!a.hasTool(t.name)) continue;
            std::string perm = t.permGroup.empty() ? "allow" : Config::get().permission(t.permGroup);
            auto it = a.permission.find(t.permGroup); if (!t.permGroup.empty() && it != a.permission.end()) perm = it->second;
            if (app.autoApprove && perm == "ask") perm = "allow*";
            if (!t.permGroup.empty() && app.alwaysAllow.count(t.permGroup) && perm == "ask") perm = "allow*";
            std::string p = UI::pad(perm, 8);
            lines.push_back(UI::pad(t.name, 12) + (perm == "deny" ? ui().err(p) : perm == "ask" ? ui().warn(p) : ui().ok(p)) + ui().muted(t.description));
        }
        ui().section(std::string("Инструменты") + (a.readOnly() ? " (режим plan)" : ""), lines);
        std::cout << "\n" << ui().muted("allow* — разрешено в этой сессии") << "\n";
    }
    else if (cmd == "/workflows") listWorkflows();
    else if (cmd == "/run") {
        auto p = arg.find(' ');
        runWorkflow(arg.substr(0, p), p == std::string::npos ? "" : trim(arg.substr(p + 1)));
    }
    else ui().error("неизвестная команда " + cmd + " — см. /help");
    return true;
}

void runShell(const std::string& cmd) {
    if (trim(cmd).empty()) return;
    auto r = runCommand(cmd, 200000, [](const std::string& l) {
        std::string s = l; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        std::cout << "  " << ui().muted("│") << " " << s << "\n" << std::flush;
    });
    std::cout << "  " << (r.code == 0 ? ui().muted("exit 0") : ui().err("exit " + std::to_string(r.code))) << "\n";
    app.session.messages.push(Json::object().set("role", "user").set("content", "Я выполнил команду `" + cmd + "` (exit " + std::to_string(r.code) + "):\n```\n" + truncateUtf8(r.output, 8000) + "\n```"));
    app.session.messages.push(Json::object().set("role", "assistant").set("content", "Принято, учту вывод команды."));
    app.session.save();
}

// ---------- projects, start screen, main menu ----------

void openProject(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::current_path(dir, ec);
    if (ec) { ui().error("не удалось открыть " + dir.string() + ": " + ec.message()); return; }
    std::string theme = ui().themeName;
    Config::get().load();
    if (!Config::get().data.getString("theme").empty()) ui().setTheme(Config::get().data.getString("theme")); else ui().setTheme(theme);
}

// No model yet, or the selected one has no key / its local server is down: offer what works.
void ensureModelReady() {
    std::string model = sessionModel();
    Provider p; bool known = false, conn = false;
    if (!model.empty()) {
        auto pid = Config::splitModel(model).first;
        known = Config::get().findProvider(pid, p);
        if (known) { providerStatus(p, conn); if (conn) return; }
    }
    std::cout << "\n";
    if (model.empty()) ui().warning("модель не выбрана — подключите API, список моделей загрузится с сервера");
    else ui().warning("модель " + model + " сейчас недоступна" + (!known ? " — нет такого подключения" : p.local() ? " — " + p.name + " не запущен" : " — нет ключа"));
    std::vector<MenuItem> items; std::vector<std::string> picks;
    for (auto& [lp, models] : localServers())
        for (size_t i = 0; i < models.size() && i < 4; ++i) { items.push_back({lp.name + " · " + models[i], "  локально, без ключа"}); picks.push_back(lp.id + "/" + models[i]); }
    items.push_back({"Подключить API / выбрать модель…", "  OpenAI, Anthropic, OpenRouter, Gemini, свой адрес"}); picks.push_back("@connect");
    items.push_back({"Продолжить без модели", ""}); picks.push_back("");
    int r = menu("Чем работать?", items, 0);
    if (r < 0 || picks[(size_t)r].empty()) return;
    std::string m = picks[(size_t)r];
    if (m == "@connect") { m = pickModel("Модель", model); if (m == "-" || m.empty()) return; }
    app.session.model = m;
    if (Config::get().model() != m && confirm("Сделать " + m + " моделью по умолчанию?", true)) Config::get().saveGlobal("model", m);
    ui().success("Модель: " + m);
}

std::vector<fs::path> listProjects() {
    std::vector<std::pair<fs::file_time_type, fs::path>> v; std::error_code ec;
    for (auto& e : fs::directory_iterator(Config::get().projectsDir(), ec))
        if (e.is_directory(ec) && e.path().filename().string()[0] != '.') v.push_back({fs::last_write_time(e.path(), ec), e.path()});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<fs::path> out; for (auto& x : v) out.push_back(x.second);
    return out;
}

void startSessionIn(const fs::path& dir, bool announce = true) {
    std::error_code ec;
    bool existed = fs::exists(dir, ec);
    bool empty = !existed || fs::is_empty(dir, ec);
    openProject(dir);
    // a brand-new (or still empty) project gets a NOVA.md template; existing code is never touched
    bool templ = false;
    if (empty && Config::get().data.getBool("project_instructions", true) && instructionsFile().empty())
        templ = writeFile(fs::current_path() / "NOVA.md", projectTemplate(fs::current_path().filename().string()));
    app.session = Session::create(Config::get().model());
    app.alwaysAllow.clear();
    if (announce) ui().success(std::string(existed ? "Открыт проект " : "Создан проект ") + ui().bold(homeShort(fs::current_path().string())));
    if (announce && templ) ui().note("NOVA.md — инструкции проекта для ассистента: заполните его или выполните /init");
    ensureModelReady();
}

bool newProjectSession() {
    fs::path base = Config::get().projectsDir();
    std::string def = Config::get().data.getString("default_project", "default project");
    std::cout << "\n";
    ui().note("Проекты лежат в " + homeShort(base.string()) + ". «.» — работать в " + homeShort(app.launchDir.string()));
    std::string name = ask("Название проекта", def);
    fs::path dir;
    if (name == ".") dir = app.launchDir;
    else if (name[0] == '~') dir = fs::path(homeDir().string() + name.substr(1));
    else if (fs::path(name).is_absolute()) dir = fs::path(name);
    else {
        if (name.find("..") != std::string::npos || name.find_first_of("/\\:*?\"<>|") != std::string::npos) { ui().error("в названии не должно быть / \\ : * ? \" < > | и .."); return false; }
        dir = base / name;
    }
    startSessionIn(dir);
    return true;
}

bool projectsMenu() {
    while (true) {
        auto ps = listProjects();
        screenTitle("Проекты");
        ui().note(homeShort(Config::get().projectsDir().string()));
        std::cout << "\n";
        std::vector<MenuItem> items;
        for (auto& p : ps) {
            std::error_code ec; size_t n = 0;
            for (auto it = fs::directory_iterator(p, ec); it != fs::directory_iterator() && n < 999; it.increment(ec)) ++n;
            items.push_back({p.filename().string(), "  " + std::to_string(n) + " " + plural((long long)n, "элемент", "элемента", "элементов") + (fs::exists(p / "NOVA.md") ? " · NOVA.md" : fs::exists(p / "AGENTS.md") ? " · AGENTS.md" : "")});
        }
        if (!ps.empty()) items.push_back({"", "", true});
        items.push_back({"Новый проект…", ""});
        items.push_back({"Назад", ""});
        int r = menu("", items, 0);
        if (r < 0 || r == (int)items.size() - 1) return false;
        if (r == (int)items.size() - 2) { if (newProjectSession()) return true; continue; }
        startSessionIn(ps[(size_t)r]);
        return true;
    }
}

bool resumeMenu() {
    auto ss = Session::list(30);
    if (ss.empty()) { ui().note("Сессий пока нет"); return false; }
    std::vector<MenuItem> items;
    for (auto& s : ss) items.push_back({ui().fit(s.title, 40), "  " + isoTime(s.updated).substr(5, 11) + "  " + homeShort(s.cwd)});
    int r = menu("Продолжить сессию", items, 0);
    if (r < 0) return false;
    Session s;
    if (!Session::load(ss[(size_t)r].id, s)) { ui().error("не удалось прочитать сессию"); return false; }
    std::error_code ec;
    if (!s.cwd.empty() && fs::is_directory(s.cwd, ec)) openProject(s.cwd);
    else ui().warning("папка сессии не найдена, остаёмся в " + homeShort(fs::current_path().string()));
    app.session = s; app.alwaysAllow.clear();
    ui().success("Продолжаем «" + s.title + "»");
    return true;
}

void settingsMenu() {
    while (true) {
        auto& c = Config::get();
        bool intro = c.data.getBool("intro", true);
        bool every = c.data.getString("intro_mode", "always") != "6h";
        double speed = c.data.getNum("animation_speed", 1.0);
        std::string speedName = speed <= 0 ? "без задержек" : speed < 0.9 ? "медленно" : speed > 1.2 ? "быстро" : "обычно";
        screenTitle("Настройки");
        std::vector<MenuItem> items = {
            {"Цветовая тема", "  " + ui().gradient(ui().themeName, false)},
            {"Папка проектов", "  " + homeShort(c.projectsDir().string())},
            {"Заставка при запуске", std::string("  ") + (!intro ? "выключена" : every ? "при каждом запуске" : "раз в 6 часов")},
            {"Модель по умолчанию", "  " + (c.model().empty() ? ui().muted("не выбрана") : c.model())},
            {"Подтверждения действий", std::string("  ") + (app.autoApprove ? ui().warn("выключены (yolo)") : "включены")},
            {"Скорость анимации", "  " + speedName},
            {"Общие инструкции", "  " + homeShort(globalInstructionsFile().string())},
            {"", "", true},
            {"Назад", ""},
        };
        int r = menu("", items, 0);
        if (r < 0 || r == 8) return;
        if (r == 0) themeMenu();
        else if (r == 1) {
            std::string p = ask("Папка проектов", homeShort(c.projectsDir().string()));
            c.saveGlobal("projects_dir", p); ui().success("Папка проектов: " + homeShort(c.projectsDir().string()));
        }
        else if (r == 2) {   // cycle: every start → every 6 hours → off → every start
            if (intro && every) { c.saveGlobal("intro_mode", Json("6h")); ui().success("Заставка: раз в 6 часов"); }
            else if (intro) { c.saveGlobal("intro", Json(false)); ui().success("Заставка выключена"); }
            else { c.saveGlobal("intro", Json(true)); c.saveGlobal("intro_mode", Json("always")); ui().success("Заставка: при каждом запуске"); }
        }
        else if (r == 5) {   // slow → normal → fast → slow (applies on the next start)
            double next = speed > 0 && speed < 0.9 ? 1.0 : speed >= 0.9 && speed <= 1.2 ? 1.5 : 0.7;
            c.saveGlobal("animation_speed", Json(next));
            ui().success(std::string("Скорость анимации: ") + (next < 0.9 ? "медленно" : next > 1.2 ? "быстро" : "обычно") + " (со следующего запуска)");
        }
        else if (r == 6) openInEditor(globalInstructionsFile(), globalTemplate());
        else if (r == 3) {
            std::string m = pickModel("Модель по умолчанию", c.model());
            if (m != "-" && !m.empty()) { c.saveGlobal("model", m); app.session.model = m; ui().success("Модель по умолчанию: " + m); }
        }
        else if (r == 4) { app.autoApprove = !app.autoApprove; }
    }
}

void workflowsMenu() {
    auto wfs = Workflows::list();
    if (wfs.empty()) { ui().note("Воркфлоу нет. Создать шаблон: nova workflow create <имя>"); return; }
    std::vector<MenuItem> items;
    for (auto& w : wfs) items.push_back({w.first, "  " + w.second.getString("description")});
    int r = menu("Запустить воркфлоу", items, 0);
    if (r < 0) return;
    runWorkflow(wfs[(size_t)r].first, ask("Входные данные (необязательно)", ""));
}

std::string localSummary() {
    std::vector<std::string> parts;
    for (auto& [p, models] : localServers()) parts.push_back(p.name + " · " + std::to_string(models.size()));
    return parts.empty() ? "" : joinStr(parts, ", ");
}

void statusBlock() {
    int conn = 0, total = 0;
    for (auto& p : Config::get().providers()) { if (!p.needsKey) continue; bool c; providerStatus(p, c); ++total; if (c) ++conn; }
    std::string local = localSummary();
    std::string model = Config::get().model();
    std::cout << "\n";
    ui().keyValues("", {
        {"модель", model.empty() ? ui().warn("не выбрана") + ui().muted("  — «Подключения API»") : model},
        {"облако", std::to_string(conn) + " из " + std::to_string(total) + " с ключом" + (conn ? "" : ui().muted("  — «Подключения API»"))},
        {"локально", local.empty() ? ui().muted("не найдено (Ollama, LM Studio, llama.cpp)") : ui().ok(local)},
        {"сессии", std::to_string(Session::count())},
        {"проекты", homeShort(Config::get().projectsDir().string())},
    });
}

void logoScreen() {
    if (fullScreen()) { ui().clearScreen(); std::cout << "\n\n\n"; Intro::printLogo(); Intro::tagline(false); statusBlock(); }
}

// Returns true when a chat session should start, false to quit.
bool mainMenu() {
    int def = 0;
    while (true) {
        std::string def_ = Config::get().data.getString("default_project", "default project");
        std::string model = Config::get().model();
        std::vector<MenuItem> items = {
            {"Новая сессия", "  проект «" + def_ + "» или новый"},
            {"Проекты", "  " + homeShort(Config::get().projectsDir().string())},
            {"Продолжить сессию", ""},
            {"Работать в текущей папке", "  " + homeShort(app.launchDir.string())},
            {"", "", true},
            {"Модель", "  " + (model.empty() ? ui().warn("не выбрана") : shortModel(model))},
            {"Подключения API", "  ключи, загрузка моделей, локальные серверы"},
            {"Воркфлоу", ""},
            {"Настройки", ""},
            {"", "", true},
            {"Выход", ""},
        };
        logoScreen();
        ui().replay();
        std::cout << "\n";
        int r = menu("Главное меню", items, def);
        if (r < 0 || r == 10) return false;
        def = r;
        if (r == 0) { if (newProjectSession()) return true; }
        else if (r == 1) { if (projectsMenu()) return true; }
        else if (r == 2) { if (resumeMenu()) return true; }
        else if (r == 3) { startSessionIn(app.launchDir, false); return true; }
        else if (r == 5) {
            std::string m = pickModel("Модель по умолчанию", Config::get().model());
            if (m != "-" && !m.empty()) { Config::get().saveGlobal("model", m); app.session.model = m; ui().success("Модель по умолчанию: " + m); }
        }
        else if (r == 6) connectionsMenu();
        else if (r == 7) workflowsMenu();
        else if (r == 8) settingsMenu();
    }
}

std::vector<Intro::Step> bootSteps() {
    using S = Intro::Status;
    return {
        {"Конфигурация", [] { return std::make_pair(S::Ok, homeShort(Config::get().globalPath.string())); }},
        {"Облачные API", [] {
            int conn = 0, total = 0; for (auto& p : Config::get().providers()) { if (!p.needsKey) continue; bool c; providerStatus(p, c); ++total; if (c) ++conn; }
            return std::make_pair(conn ? S::Ok : S::Warn, conn ? std::to_string(conn) + " из " + std::to_string(total) + " с ключом" : std::string("не подключены — можно подключить в меню")); }},
        {"Локальные модели", [] {
            LLM::probeLocal();
            std::string l = localSummary();
            return l.empty() ? std::make_pair(S::Warn, std::string("Ollama, LM Studio, llama.cpp не запущены")) : std::make_pair(S::Ok, l); }},
        {"Модель", [] {
            std::string m = Config::get().model();
            return m.empty() ? std::make_pair(S::Warn, std::string("не выбрана — подключите API")) : std::make_pair(S::Ok, m); }},
        {"curl", [] {
            auto r = runCommand("curl --version", 2000, nullptr, 5000);
            return r.code == 0 ? std::make_pair(S::Ok, split(firstLine(r.output), ' ').size() > 1 ? "версия " + split(firstLine(r.output), ' ')[1] : std::string("найден"))
                               : std::make_pair(S::Fail, std::string("не найден — нужен для запросов к облачным API")); }},
        {"git", [] {
            auto r = runCommand("git --version", 2000, nullptr, 5000);
            return r.code == 0 ? std::make_pair(S::Ok, firstLine(r.output)) : std::make_pair(S::Warn, std::string("не найден, необязательно")); }},
        {"Папка проектов", [] {
            std::error_code ec; fs::path p = Config::get().projectsDir(); fs::create_directories(p, ec);
            if (!ec) ensureProjectsGuide(p);
            return ec ? std::make_pair(S::Fail, ec.message()) : std::make_pair(S::Ok, homeShort(p.string())); }},
        {"Сессии", [] { return std::make_pair(S::Ok, std::to_string(Session::count()) + " сохранено"); }},
    };
}

// Intro → logo → "Создать новую сессию?" → menu. Returns false to quit.
bool startScreen(bool first, bool intro) {
    if (first && intro) {
        // "intro_mode": "always" (default) — the full forge animation on every start;
        // "6h" — at most every 6 hours, other starts show the logo at once
        fs::path stamp = novaHome() / "tmp" / "intro.stamp";
        std::error_code ec; bool animate = true;
        if (Config::get().data.getString("intro_mode", "always") == "6h" && fs::exists(stamp, ec)) {
            auto age = fs::file_time_type::clock::now() - fs::last_write_time(stamp, ec);
            animate = ec || age > std::chrono::hours(6) || age < std::chrono::hours(0);
        }
        if (animate) { fs::create_directories(stamp.parent_path(), ec); writeFile(stamp, isoTime()); }
        Intro::run(bootSteps(), animate); statusBlock();
    }
    else logoScreen();
    if (first) {
        std::cout << "\n";
        if (confirm("Создать новую сессию?", true)) { if (newProjectSession()) return true; }
    }
    return mainMenu();
}

struct Cmd { const char* name; const char* hint; bool args; };
const std::vector<Cmd>& commands() {
    static const std::vector<Cmd> c = {
        {"/help", "команды и клавиши", false}, {"/menu", "главное меню", false}, {"/model", "сменить модель", true}, {"/models", "список моделей", false},
        {"/connect", "подключения API и локальные модели", false}, {"/plan", "режим plan: только чтение (вкл/выкл)", false}, {"/new", "новая сессия", false},
        {"/sessions", "список сессий", false}, {"/resume", "продолжить сессию", true}, {"/undo", "откатить последний ход и файлы", false},
        {"/redo", "вернуть откаченный ход", false}, {"/compact", "сжать историю", false}, {"/export", "сохранить сессию в Markdown", true},
        {"/cost", "расход токенов", false}, {"/todos", "список задач", false}, {"/init", "создать или обновить NOVA.md", false}, {"/instructions", "инструкции для ассистента", true},
        {"/details", "инструменты и разрешения", false}, {"/workflows", "воркфлоу", false}, {"/run", "запустить воркфлоу", true},
        {"/project", "папка проекта", false}, {"/yolo", "вкл/выкл подтверждения", false}, {"/theme", "цветовая тема", true},
        {"/clear", "очистить экран", false}, {"/exit", "выход", false},
    };
    return c;
}

// Files of the project for @-completion (cached for a few seconds).
const std::vector<std::string>& projectFiles() {
    static std::vector<std::string> files; static long long at = 0; static std::string dir;
    std::string cwd = fs::current_path().string();
    if (nowMs() - at < 5000 && dir == cwd) return files;
    files.clear(); at = nowMs(); dir = cwd;
    std::error_code ec; size_t n = 0;
    fs::recursive_directory_iterator it(cwd, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && n < 4000; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it.depth() > 5) { it.disable_recursion_pending(); continue; }
        std::string name = it->path().filename().string();
        bool dir2 = it->is_directory(ec);
        if (dir2 && (isIgnoredDir(name) || name[0] == '.')) { it.disable_recursion_pending(); continue; }
        if (dir2) continue;
        files.push_back(fs::relative(it->path(), cwd, ec).generic_string()); ++n;
    }
    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    return files;
}

std::vector<Suggestion> completeInput(const std::string& before, size_t& ts) {
    std::vector<Suggestion> out;
    if (!before.empty() && before[0] == '/' && before.find_first_of(" \n") == std::string::npos) {
        ts = 0;
        for (auto& c : commands()) if (startsWith(c.name, before)) out.push_back({c.name, c.hint, !c.args});
        if (out.empty()) for (auto& c : commands()) if (std::string(c.name).find(before.substr(1)) != std::string::npos) out.push_back({c.name, c.hint, !c.args});
        if (out.size() == 1 && out[0].value == before) out.clear();
        return out;
    }
    size_t sp = before.find_last_of(" \n");
    ts = sp == std::string::npos ? 0 : sp + 1;
    std::string tok = before.substr(ts);
    if (tok.size() < 1 || tok[0] != '@') return out;
    std::string q = lower(tok.substr(1));
    std::vector<Suggestion> pre, sub;
    for (auto& f : projectFiles()) {
        std::string lf = lower(f), base = lf.substr(lf.find_last_of('/') == std::string::npos ? 0 : lf.find_last_of('/') + 1);
        if (startsWith(base, q) || startsWith(lf, q)) pre.push_back({"@" + f, ""});
        else if (!q.empty() && lf.find(q) != std::string::npos) sub.push_back({"@" + f, ""});
        if (pre.size() >= 8) break;
    }
    for (auto& x : pre) out.push_back(x);
    for (auto& x : sub) out.push_back(x);
    return out;
}

// Shift+Tab: обычный → авто-подтверждение → plan → обычный.
void cycleMode() {
    if (app.plan) { app.plan = false; app.autoApprove = false; }
    else if (!app.autoApprove) app.autoApprove = true;
    else { app.autoApprove = false; app.plan = true; }
}

std::string footerLeft() {
    if (app.plan) return ui().accent(std::string(gl::paused) + " режим plan") + ui().muted(" — только чтение и план (shift+tab)");
    if (app.autoApprove) return ui().warn(std::string(gl::fast) + " авто-подтверждение") + ui().muted(" (shift+tab)");
    return ui().muted("? — подсказки");
}
std::string footerRight() {
    std::string m = sessionModel();
    long long t = app.session.tokensIn + app.session.tokensOut;
    if (m.empty()) return ui().warn("модель не выбрана") + ui().muted(" · /connect");
    return ui().muted(shortModel(m) + (t ? " · " + tokens(t) + " ток." : ""));
}

void chat() {
    if (fullScreen()) { ui().clearScreen(); std::cout << "\n"; }
    printHeader();
    ui().replay();
    app.toMenu = false;
    LineEditor ed;
    ed.loadHistory();
    ed.complete = completeInput;
    ed.footerLeft = footerLeft; ed.footerRight = footerRight;
    ed.onShiftTab = cycleMode;
    ed.onClear = [] { ui().clearScreen(); printHeader(); std::cout << "\n"; };
    ed.shortcuts = {"/ — команды", "@ — файлы", "! — команда shell", "\\ + Enter — новая строка", "Alt+Enter, Ctrl+J — новая строка",
                    "Tab — дополнить", "↑ ↓ — история", "Shift+Tab — режим", "Esc — прервать ответ", "Esc Esc — очистить ввод",
                    "Ctrl+L — очистить экран", "Ctrl+C дважды — выход"};
    while (true) {
        ui().forget();
        std::cout << "\n";
        EditorResult er = ed.read();
        if (!er.ok) break;
        std::string input = trim(er.text);
        if (input.empty()) continue;
        if (input == "?") { printHelp(); continue; }
        if (input[0] == '/') { if (!handleSlash(input)) break; continue; }
        if (input[0] == '!') { runShell(input.substr(1)); continue; }
        runTurn(expandMentions(input));
    }
    app.spinner.stop();
}

// withMenu: full-screen start (intro, menu). Without a terminal, or with -c, chat starts directly.
void interactive(bool withMenu, bool intro) {
    if (!withMenu) { chat(); if (Term::interactive()) Intro::farewell(); return; }
    bool first = true;
    while (true) {
        if (!startScreen(first, intro)) break;
        first = false;
        chat();
        if (!app.toMenu) break;
    }
    std::cout << "\n";
    Intro::farewell();
}

// ---------- CLI ----------
struct Args {
    std::vector<std::string> pos; std::map<std::string, std::string> opt;
    bool has(const std::string& k) const { return opt.count(k) > 0; }
    std::string get(const std::string& k, const std::string& d = "") const { auto it = opt.find(k); return it == opt.end() ? d : it->second; }
};
Args parseArgs(int argc, char** argv) {
    Args a;
    static const std::set<std::string> flags = {"yes", "y", "json", "continue", "c", "global", "help", "h", "version", "v", "no-color", "no-intro", "no-menu", "plan", "refresh"};
    bool rest = false;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (rest) { a.pos.push_back(s); continue; }
        if (s == "--") { rest = true; continue; }
        if (startsWith(s, "--") || (s.size() == 2 && s[0] == '-' && s[1] != '-')) {
            std::string k = s.substr(s[1] == '-' ? 2 : 1); std::string v;
            auto eq = k.find('='); if (eq != std::string::npos) { v = k.substr(eq + 1); k = k.substr(0, eq); a.opt[k] = v; continue; }
            if (!flags.count(k) && i + 1 < argc) v = argv[++i]; else v = "1";
            a.opt[k] = v;
        } else a.pos.push_back(s);
    }
    return a;
}

void usage() {
    std::cout << ui().bold("NovaForge " + std::string(VERSION)) << " — AI-ассистент для разработки и автоматизации в терминале\n";
    struct C { const char* c; const char* d; };
    auto group = [](const char* title, const std::vector<C>& cs) {
        std::vector<std::string> lines;
        for (auto& c : cs) lines.push_back(UI::pad(c.c, 38) + ui().muted(c.d));
        ui().section(title, lines);
    };
    group("Использование", {
        {"nova", "заставка, главное меню и диалог"}, {"nova \"задача\"", "то же, что nova run"},
        {"nova run \"задача\"", "выполнить задачу и выйти"},
        {"nova serve", "REST API и планировщик воркфлоу"},
        {"nova daemon", "только планировщик воркфлоу"},
        {"nova workflow list|create|show|run", "воркфлоу (run <имя> --input \"…\")"},
        {"nova session list|show|export|delete", "сессии"},
        {"nova auth login|list|logout", "ключи провайдеров"},
        {"nova models [--refresh]", "модели подключённых API"}, {"nova connect", "подключения API"}, {"nova init", "создать NOVA.md"},
        {"nova config show|get|set|path", "настройки"}, {"nova doctor", "проверить окружение"},
    });
    group("Флаги", {
        {"-m, --model <провайдер/модель>", "модель для этого запуска"}, {"--plan", "только чтение и план, без правок"},
        {"-c, --continue", "продолжить последнюю сессию"}, {"-y, --yes", "не спрашивать подтверждений"},
        {"--json", "вывод nova run в JSON"}, {"--port, --host", "адрес для nova serve"},
        {"--theme <имя>", "forge, amber, phosphor, steel, default, mono"}, {"--no-intro", "без заставки"}, {"--no-menu", "сразу диалог в текущей папке"}, {"--cwd <папка>", "начать в указанной папке"}, {"--no-color", "без цветов"}, {"-v, --version", "версия"},
    });
    std::cout << "\n" << ui().muted("Конфигурация: ~/.novaforge/config.json и ./novaforge.json") << "\n";
}

void resumeLast() { auto ss = Session::list(1); Session s; if (!ss.empty() && Session::load(ss[0].id, s)) app.session = s; }

int cmdRun(const Args& a) {
    std::string prompt;
    for (size_t i = 1; i < a.pos.size(); ++i) prompt += (i > 1 ? " " : "") + a.pos[i];
    if (!isStdinTTY()) {
        std::string in((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        if (!trim(in).empty()) prompt += "\n\n<stdin>\n" + in + "\n</stdin>";
    }
    if (trim(prompt).empty()) { ui().error("укажите задачу: nova run \"…\""); return 2; }
    if (a.has("json")) {
        EngineEvents ev; bool aa = app.autoApprove;
        ev.askPermission = [aa](const std::string&, const std::string&, const std::string&) { return aa; };
        app.session.beginTurn(); auto r = Engine::run(app.session, currentMode(), prompt, ev); app.session.endTurn(); app.session.save();
        Json j = Json::object(); j["ok"] = r.ok; j["reply"] = r.text; j["error"] = r.error; j["steps"] = r.steps; j["tools"] = r.toolLog; j["session_id"] = app.session.id;
        std::cout << j.dump(2) << "\n"; return r.ok ? 0 : 1;
    }
    return runTurn(expandMentions(prompt)) ? 0 : 1;
}

int cmdWorkflow(const Args& a) {
    std::string sub = a.pos.size() > 1 ? a.pos[1] : "list";
    std::string name = a.pos.size() > 2 ? a.pos[2] : "";
    if (sub == "list") { listWorkflows(); return 0; }
    if (sub == "create" || sub == "new") {
        if (name.empty()) name = ask("Имя воркфлоу", "review");
        if (!Workflows::validName(name)) { ui().error("некорректное имя: латиница, цифры, '-', '_' и '.'"); return 1; }
        Json existing;
        if (Workflows::find(name, existing) && !confirm("Воркфлоу " + name + " уже есть. Перезаписать?", false)) return 1;
        fs::path p = Workflows::save(Workflows::templateWorkflow(name));
        ui().success("Создан шаблон " + p.string());
        ui().note("Отредактируйте шаги и запустите: nova workflow run " + name);
        return 0;
    }
    if (sub == "show") { Json wf; if (!Workflows::find(name, wf)) { ui().error("воркфлоу не найден: " + name); return 1; } std::cout << wf.dump(2) << "\n"; return 0; }
    if (sub == "run") return runWorkflow(name, a.get("input")) ? 0 : 1;
    ui().error("использование: nova workflow list | create <имя> | show <имя> | run <имя> [--input …] [--yes]"); return 2;
}

int cmdSession(const Args& a) {
    std::string sub = a.pos.size() > 1 ? a.pos[1] : "list";
    std::string id = a.pos.size() > 2 ? a.pos[2] : "";
    if (sub == "list") { listSessions(); return 0; }
    if (sub != "show" && sub != "export" && sub != "delete" && sub != "rm") {
        ui().error("использование: nova session list | show <id> | export <id> [файл] | delete <id>"); return 2;
    }
    Session s; if (!Session::load(id, s)) { ui().error("сессия не найдена: " + id); return 1; }
    if (sub == "show") { MarkdownStream m; m.feed(s.exportMarkdown()); m.flush(); return 0; }
    if (sub == "export") {
        std::string f = a.pos.size() > 3 ? a.pos[3] : s.id + ".md";
        if (!writeFile(f, s.exportMarkdown())) { ui().error("не удалось записать " + f); return 1; }
        ui().success("Сохранено в " + f); return 0;
    }
    Session::remove(id); ui().success("Сессия " + id + " удалена"); return 0;
}

int cmdAuth(const Args& a) {
    std::string sub = a.pos.size() > 1 ? a.pos[1] : "list";
    std::string prov = a.pos.size() > 2 ? a.pos[2] : "";
    if (sub == "login") return authLogin(prov) ? 0 : 1;
    if (sub == "logout") {
        if (prov.empty()) { ui().error("использование: nova auth logout <провайдер>"); return 2; }
        if (Config::get().removeKey(prov)) ui().success("Ключ " + prov + " удалён"); else ui().note("Сохранённого ключа для " + prov + " нет");
        return 0;
    }
    std::vector<std::string> lines;
    for (auto& p : Config::get().providers()) {
        std::string src = Config::get().auth().has(p.id) ? "auth.json" : (!p.envKey.empty() && !env(p.envKey.c_str()).empty()) ? "$" + p.envKey : "";
        std::string status = src.empty() ? (p.needsKey ? ui().muted("не подключён") : ui().muted("локальный")) : ui().ok(src);
        lines.push_back(UI::pad(p.name, 24) + UI::pad(ui().muted(p.id), 14) + status);
    }
    ui().section("Провайдеры", lines); return 0;
}

int cmdConfig(const Args& a) {
    std::string sub = a.pos.size() > 1 ? a.pos[1] : "show";
    auto& c = Config::get();
    if (sub == "path") { std::cout << c.globalPath.string() << "\n" << c.projectPath.string() << "\n"; return 0; }
    if (sub == "get") {
        if (a.pos.size() < 3) { ui().error("использование: nova config get <ключ>"); return 2; }
        if (!c.data.has(a.pos[2])) { ui().error("нет ключа " + a.pos[2]); return 1; }
        std::cout << c.data.get(a.pos[2]).dump(2) << "\n"; return 0;
    }
    if (sub == "set") {
        if (a.pos.size() < 4) { ui().error("использование: nova config set <ключ> <значение>"); return 2; }
        Json v; if (!Json::tryParse(a.pos[3], v)) v = Json(a.pos[3]);
        c.saveGlobal(a.pos[2], v); ui().success(a.pos[2] + " = " + v.dump()); return 0;
    }
    std::cout << c.data.dump(2) << "\n"; return 0;
}

int cmdDoctor() {
    std::vector<std::string> lines; bool allOk = true;
    auto check = [&](bool ok, bool required, const std::string& name, const std::string& detail) {
        if (!ok && required) allOk = false;
        lines.push_back((ok ? ui().ok("ok  ") : required ? ui().err("нет ") : ui().warn("нет ")) + UI::pad(name, 12) + ui().muted(detail));
    };
    auto curl = runCommand("curl --version", 2000);
    check(curl.code == 0, true, "curl", curl.code == 0 ? ui().fit(firstLine(curl.output), 60) : "нужен для запросов к облачным API");
    auto git = runCommand("git --version", 2000);
    check(git.code == 0, false, "git", git.code == 0 ? firstLine(git.output) : "необязательно");
    int conn = 0; for (auto& p : Config::get().providers()) if (p.needsKey && !Config::get().apiKey(p).empty()) ++conn;
    check(conn > 0, false, "провайдеры", conn ? std::to_string(conn) + " подключено" : "выполните nova connect");
    LLM::probeLocal();
    std::string local = localSummary();
    check(!local.empty(), false, "локально", local.empty() ? "Ollama, LM Studio, llama.cpp не запущены" : local);
    check(!Config::get().model().empty(), false, "модель", Config::get().model().empty() ? "не выбрана — nova connect" : Config::get().model());
    std::string instr = instructionsFile();
    check(!instr.empty(), false, "NOVA.md", instr.empty() ? "выполните nova init" : instr + " найден");
    ui().section("Проверка окружения", lines);
    ui().keyValues("", {
        {"воркфлоу", std::to_string(Workflows::list().size())},
        {"конфиг", homeShort(Config::get().globalPath.string())},
    });
    return allOk ? 0 : 1;
}

int cmdDaemon() {
    std::cout << ui().bold("nova daemon") << ui().muted("  выполняет воркфлоу с триггерами interval и watch. Ctrl+C — выход.") << "\n";
    listWorkflows();
    std::cout << "\n";
    Scheduler::get().autoApprove = app.autoApprove;
    Scheduler::get().log = [](const std::string& l) { std::cout << ui().muted(isoTime().substr(11)) << "  " << l << std::endl; };
    Scheduler::get().start();
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}

extern "C" void onSigint(int) {
    if (g_inTurn) { cancelFlag() = true; return; }   // stops the current turn, not the program
    Term::restoreFromSignal();
    static const char msg[] = "\x1b[0m\x1b[?25h\n";
#ifdef _WIN32
    fputs(msg, stdout);
#else
    if (write(1, msg, sizeof msg - 1) < 0) {}
#endif
    std::_Exit(130);
}

} // namespace

// Settings chosen in the Windows installer. Setup writes installer-settings.json into the data
// folder; the next start applies it to the global config (and the API key to auth.json) and
// deletes the file. Values from the installer win over the same keys in config.json.
void importInstallerSettings() {
    std::vector<fs::path> files = {novaHome() / "installer-settings.json"};
#ifdef _WIN32
    fs::path roaming = knownFolder(FOLDERID_RoamingAppData);
    if (!roaming.empty() && roaming / "NovaForge" != novaHome()) files.push_back(roaming / "NovaForge" / "installer-settings.json");
#endif
    auto& c = Config::get();
    for (auto& f : files) {
        std::error_code ec;
        if (!fs::exists(f, ec)) continue;
        Json j = Config::readJson(f);
        const Json& conf = j.get("config");
        if (conf.isObject())
            for (auto& kv : conf.obj()) {
                if (kv.second.isObject() && c.data.get(kv.first).isObject()) {   // e.g. "permission": merge, do not replace
                    Json merged = c.data.get(kv.first); Config::merge(merged, kv.second); c.saveGlobal(kv.first, merged);
                } else c.saveGlobal(kv.first, kv.second);
            }
        std::string prov = j.getString("provider"), key = trim(j.getString("api_key"));
        if (!prov.empty() && !key.empty()) c.setKey(prov, key);
        fs::remove(f, ec);
    }
}

int main(int argc, char** argv) {
    ui().init();
    Args a = parseArgs(argc, argv);
    if (a.has("no-color")) ui().color = false;
    try {
        Config::get().load();
        importInstallerSettings();
        ui().setTheme(Config::get().data.getString("theme", "default"));
        gl::configure(Config::get().data.getString("glyphs", "auto"));
        if (a.has("theme") && !ui().setTheme(a.get("theme"))) ui().warning("нет темы " + a.get("theme") + ", используется default");
    } catch (const std::exception& e) { ui().error(std::string("ошибка конфигурации: ") + e.what()); return 1; }

    if (a.has("cwd")) {   // used by the Explorer context menu "Открыть NovaForge здесь"
        std::error_code ec; fs::current_path(fs::path(a.get("cwd")), ec);
        if (ec) { ui().error("не удалось перейти в " + a.get("cwd") + ": " + ec.message()); return 2; }
        Config::get().load();
    }
    app.launchDir = fs::current_path();
    std::string cmd = a.pos.empty() ? "" : a.pos[0];
    if (a.has("version") || a.has("v") || cmd == "version") { std::cout << "NovaForge " << VERSION << "\n"; return 0; }
    if (a.has("help") || a.has("h") || cmd == "help") { usage(); return 0; }

    app.autoApprove = a.has("yes") || a.has("y");
    app.plan = a.has("plan");
    std::string modelOverride = a.get("model", a.get("m"));
    if (!modelOverride.empty()) {
        std::string m = resolveModel(modelOverride);
        if (m.empty()) { ui().error("модель «" + modelOverride + "» не найдена: укажите провайдер/модель (список: nova models)"); return 2; }
        Config::get().data["model"] = m;
    }
    app.session = Session::create(Config::get().model());
    if ((cmd.empty() || cmd == "run") && (a.has("continue") || a.has("c"))) {
        resumeLast();
        std::error_code ec;
        if (!app.session.cwd.empty() && fs::is_directory(app.session.cwd, ec) && cmd.empty()) openProject(app.session.cwd);
        if (!modelOverride.empty()) app.session.model = Config::get().model();
    }

    std::signal(SIGINT, onSigint);

    try {
        if (cmd.empty()) {
            bool resumed = a.has("continue") || a.has("c");
            bool withMenu = Term::interactive() && !resumed && !a.has("no-menu");
            bool intro = withMenu && ui().color && Config::get().data.getBool("intro", true) && !a.has("no-intro");
            interactive(withMenu, intro);
            return 0;
        }
        if (cmd == "run") return cmdRun(a);
        if (cmd == "serve" || cmd == "server") {
            Server s; const Json& sc = Config::get().data.get("server");
            try { s.port = std::stoi(a.get("port", std::to_string((int)sc.getNum("port", 4096)))); }
            catch (...) { ui().error("некорректный --port"); return 2; }
            if (s.port < 1 || s.port > 65535) { ui().error("порт должен быть от 1 до 65535"); return 2; }
            s.host = a.get("host", sc.getString("host", "127.0.0.1"));
            s.apiKey = env("NOVA_SERVER_KEY", sc.getString("api_key"));
            s.autoApprove = app.autoApprove;
            return s.run();
        }
        if (cmd == "agent" || cmd == "agents") { ui().error("агентов больше нет: NovaForge работает как один ассистент. Только чтение и план — --plan или Shift+Tab в чате"); return 2; }
        if (cmd == "workflow" || cmd == "workflows" || cmd == "wf") return cmdWorkflow(a);
        if (cmd == "session" || cmd == "sessions") return cmdSession(a);
        if (cmd == "auth") return cmdAuth(a);
        if (cmd == "models") { listModels(a.has("refresh") || (a.pos.size() > 1 && a.pos[1] == "refresh")); return 0; }
        if (cmd == "connect" || cmd == "connections") { connectionsMenu(); return 0; }
        if (cmd == "intro") { Intro::run(bootSteps()); return 0; }
        if (cmd == "providers") return cmdAuth(Args{{"auth", "list"}, {}});
        if (cmd == "config") return cmdConfig(a);
        if (cmd == "doctor") return cmdDoctor();
        if (cmd == "daemon") return cmdDaemon();
        if (cmd == "init") return doInit() ? 0 : 1;
        // Unknown first argument is treated as a prompt: nova "fix the bug"
        Args r = a; r.pos.insert(r.pos.begin(), "run"); return cmdRun(r);
    } catch (const std::exception& e) { app.spinner.stop(); ui().error(e.what()); return 1; }
}
