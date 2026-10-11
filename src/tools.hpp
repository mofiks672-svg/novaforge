// NovaForge — built-in tools exposed to the model (function calling)
#pragma once
#include "mode.hpp"
#include "session.hpp"
#include "ui.hpp"
#include <regex>

namespace nova {

struct ToolResult { bool ok = true; std::string output; std::string title; };

// Comment the user gave when declining an action; passed back to the model.
inline std::string& permissionFeedback() { static std::string s; return s; }

struct ToolContext {
    Session* session = nullptr;
    const Mode* mode = nullptr;
    std::function<bool(const std::string& group, const std::string& title, const std::string& detail)> askPermission;
    std::function<void(const std::string&)> onOutput;
};

struct Tool {
    std::string name, description;
    Json parameters;
    std::string permGroup;  // "", "edit", "bash", "webfetch"
    std::function<ToolResult(const Json&, ToolContext&)> run;
};

namespace detail {

inline Json schema(std::initializer_list<std::tuple<const char*, const char*, const char*, bool>> props) {
    Json s = Json::object(); s["type"] = "object";
    Json p = Json::object(); Json req = Json::array();
    for (auto& [name, type, desc, required] : props) {
        Json d = Json::object(); d["type"] = type; d["description"] = desc;
        if (std::string(type) == "array") { Json it = Json::object(); it["type"] = "object"; d["items"] = it; }
        p[name] = d; if (required) req.push(name);
    }
    s["properties"] = p; s["required"] = req; return s;
}

inline fs::path resolve(const std::string& p) {
    fs::path x(p.empty() ? "." : p);
    if (x.is_relative()) x = fs::current_path() / x;
    return x.lexically_normal();
}
inline std::string rel(const fs::path& p) {
    std::error_code ec; auto r = fs::relative(p, fs::current_path(), ec);
    std::string s = ec || r.empty() ? p.string() : r.generic_string();
    return s;
}

inline std::regex globToRegex(const std::string& g) {
    std::string r = "^"; int braces = 0;
    for (size_t i = 0; i < g.size(); ++i) {
        char c = g[i];
        if (c == '*') {
            if (i + 1 < g.size() && g[i + 1] == '*') {
                if (i + 2 < g.size() && g[i + 2] == '/') { r += "(.*/)?"; i += 2; } else { r += ".*"; ++i; }
            } else r += "[^/]*";
        } else if (c == '?') r += "[^/]";
        else if (c == '{') { r += "("; ++braces; }
        else if (c == '}' && braces > 0) { r += ")"; --braces; }
        else if (c == ',' && braces > 0) r += "|";
        else if (std::string(".+()^$|[]{}\\").find(c) != std::string::npos) { r += '\\'; r += c; }
        else r += c;
    }
    while (braces-- > 0) r += ")";
    r += "$";
    return std::regex(r, std::regex::icase);
}

inline void walk(const fs::path& root, const std::function<bool(const fs::path&)>& fn, size_t limit = 20000) {
    std::error_code ec; size_t n = 0;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && n < limit; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const auto& e = *it;
        if (e.is_directory(ec)) { if (isIgnoredDir(e.path().filename().string())) it.disable_recursion_pending(); continue; }
        ++n; if (!fn(e.path())) break;
    }
}

inline std::string stripHtml(const std::string& html) {
    std::string out; out.reserve(html.size() / 2);
    std::string low = lower(html);
    size_t i = 0;
    auto blockTag = [](const std::string& t) {
        static const char* b[] = {"br", "p", "/p", "div", "/div", "li", "/li", "tr", "/tr", "h1", "h2", "h3", "h4", "h5", "h6",
                                  "/h1", "/h2", "/h3", "/h4", "/h5", "/h6", "pre", "/pre", "table", "/table"};
        for (auto* x : b) if (t == x) return true;
        return false;
    };
    while (i < html.size()) {
        if (html[i] == '<') {
            if (low.compare(i, 4, "<!--") == 0) { size_t e = html.find("-->", i + 4); i = e == std::string::npos ? html.size() : e + 3; continue; }
            size_t e = html.find('>', i);
            if (e == std::string::npos) break;
            std::string tag = low.substr(i + 1, e - i - 1);
            size_t sp = tag.find_first_of(" \t\r\n/"); std::string name = tag.substr(0, sp == 0 ? tag.find_first_of(" \t\r\n>", 1) : sp);
            if (!name.empty() && name.back() == '/') name.pop_back();
            if (name == "script" || name == "style" || name == "noscript" || name == "svg") {
                size_t close = low.find("</" + name, e);
                i = close == std::string::npos ? html.size() : low.find('>', close) == std::string::npos ? html.size() : low.find('>', close) + 1;
                continue;
            }
            if (blockTag(name)) out += '\n';
            i = e + 1; continue;
        }
        out += html[i++];
    }
    static const std::pair<const char*, const char*> ents[] = {{"&nbsp;", " "}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"}, {"&mdash;", "—"}, {"&ndash;", "–"}, {"&hellip;", "…"}, {"&amp;", "&"}};
    for (auto& e : ents) out = replaceAll(out, e.first, e.second);
    // collapse whitespace: spaces within lines, at most one blank line
    std::string res; int nl = 0; bool space = false;
    for (char c : out) {
        if (c == '\r') continue;
        if (c == '\n') { space = false; if (nl < 2) res += '\n'; ++nl; continue; }
        if (c == ' ' || c == '\t') { space = true; continue; }
        if (space && nl == 0 && !res.empty()) res += ' ';
        space = false; nl = 0; res += c;
    }
    return trim(res);
}

inline ToolResult fail(const std::string& m) { return {false, "Ошибка: " + m, ""}; }

inline std::string diffPreview(const std::string& oldS, const std::string& newS) {
    auto& ui = UI::get(); std::string o;
    auto a = split(oldS, '\n'), b = split(newS, '\n');
    for (size_t i = 0; i < a.size() && i < 8; ++i) o += ui.err("- " + a[i]) + "\n";
    if (a.size() > 8) o += ui.muted("  … ещё " + countOf((long long)a.size() - 8, "строка", "строки", "строк")) + "\n";
    for (size_t i = 0; i < b.size() && i < 8; ++i) o += ui.ok("+ " + b[i]) + "\n";
    if (b.size() > 8) o += ui.muted("  … ещё " + countOf((long long)b.size() - 8, "строка", "строки", "строк")) + "\n";
    return o;
}

inline ToolResult applyEdit(const fs::path& p, const std::string& oldS, const std::string& newS, bool all, std::string& content) {
    if (oldS.empty()) return fail("old_string пуст");
    size_t n = countOccurrences(content, oldS);
    if (n == 0) return fail("old_string не найден в " + rel(p) + ". Сначала прочитайте файл через read и скопируйте текст точно.");
    if (n > 1 && !all) return fail("old_string встречается " + std::to_string(n) + " раз(а). Добавьте контекст или replace_all=true.");
    if (all) content = replaceAll(content, oldS, newS);
    else content.replace(content.find(oldS), oldS.size(), newS);
    return {true, std::to_string(all ? n : 1) + " замен(а)", ""};
}

} // namespace detail

class ToolRegistry {
public:
    static ToolRegistry& get() { static ToolRegistry r; return r; }
    const std::vector<Tool>& all() const { return tools_; }
    const Tool* find(const std::string& n) const { for (auto& t : tools_) if (t.name == n) return &t; return nullptr; }

    Json schemasFor(const Mode& a) const {
        Json arr = Json::array();
        for (auto& t : tools_) {
            if (!a.hasTool(t.name)) continue;
            if (!t.permGroup.empty()) { auto it = a.permission.find(t.permGroup); if (it != a.permission.end() && it->second == "deny") continue; }
            Json fn = Json::object(); fn["name"] = t.name; fn["description"] = t.description; fn["parameters"] = t.parameters;
            arr.push(Json::object().set("type", "function").set("function", fn));
        }
        return arr;
    }

    ToolResult execute(const std::string& name, const std::string& argStr, ToolContext& ctx) const {
        const Tool* t = find(name);
        if (!t) return detail::fail("неизвестный инструмент " + name);
        if (ctx.mode && !ctx.mode->hasTool(name)) return detail::fail("инструмент " + name + " недоступен в режиме " + ctx.mode->name);
        Json args; if (!Json::tryParse(argStr.empty() ? "{}" : argStr, args) || !args.isObject()) return detail::fail("аргументы не являются JSON-объектом");
        if (!t->permGroup.empty()) {
            std::string mode = Config::get().permission(t->permGroup);
            if (ctx.mode) { auto it = ctx.mode->permission.find(t->permGroup); if (it != ctx.mode->permission.end()) mode = it->second; }
            if (mode == "deny") return detail::fail("действие запрещено политикой (" + t->permGroup + ": deny)");
            if (mode == "ask" && ctx.askPermission) {
                std::string title, detailS;
                if (name == "bash") { title = "Выполнить команду"; detailS = args.getString("command"); }
                else if (name == "webfetch") { title = "Загрузить URL"; detailS = args.getString("url"); }
                else if (name == "write") { title = "Записать файл " + args.getString("path"); detailS = truncateUtf8(args.getString("content"), 400); }
                else if (name == "edit") { title = "Изменить " + args.getString("path"); detailS = detail::diffPreview(args.getString("old_string"), args.getString("new_string")); }
                else { title = name + " " + args.getString("path"); detailS = ""; }
                if (!ctx.askPermission(t->permGroup, title, detailS)) {
                    std::string fb = permissionFeedback(); permissionFeedback().clear();
                    return detail::fail("пользователь отклонил действие" + (fb.empty() ? std::string() : ". Комментарий пользователя: " + fb));
                }
            }
        }
        try { return t->run(args, ctx); }
        catch (const std::exception& e) { return detail::fail(e.what()); }
    }

private:
    std::vector<Tool> tools_;

    ToolRegistry() {
        using namespace detail;

        tools_.push_back({"read", "Прочитать файл. Возвращает строки с номерами. Используй offset/limit для больших файлов.",
            schema({{"path", "string", "Путь к файлу", true}, {"offset", "integer", "Номер первой строки (с 1)", false}, {"limit", "integer", "Сколько строк (по умолчанию 2000)", false}}), "",
            [](const Json& a, ToolContext&) -> ToolResult {
                fs::path p = resolve(a.getString("path"));
                if (!fs::exists(p)) return fail("файл не найден: " + rel(p));
                if (fs::is_directory(p)) return fail("это директория, используйте list");
                std::string c = readFile(p);
                if (isBinaryContent(c)) return fail("бинарный файл (" + std::to_string(c.size()) + " байт)");
                auto lines = split(c, '\n');
                double offD = a.getNum("offset", 1), limD = a.getNum("limit", 2000);
                size_t off = offD < 1 ? 1 : (size_t)offD;
                size_t lim = limD < 1 ? 2000 : (size_t)std::min(limD, 100000.0);
                if (off > lines.size() && !lines.empty()) return fail("offset " + std::to_string(off) + " за пределами файла (" + std::to_string(lines.size()) + " строк)");
                std::ostringstream o;
                for (size_t i = off - 1; i < lines.size() && i < off - 1 + lim; ++i) {
                    char num[16]; snprintf(num, sizeof num, "%6zu\t", i + 1);
                    o << num << truncateUtf8(lines[i], 2000) << "\n";
                }
                if (off - 1 + lim < lines.size()) o << "… (всего " << lines.size() << " строк, продолжите с offset=" << off + lim << ")\n";
                return {true, o.str(), rel(p) + " · " + countOf((long long)lines.size(), "строка", "строки", "строк")};
            }});

        tools_.push_back({"write", "Создать или полностью перезаписать файл.",
            schema({{"path", "string", "Путь к файлу", true}, {"content", "string", "Полное содержимое", true}}), "edit",
            [](const Json& a, ToolContext& ctx) -> ToolResult {
                if (a.getString("path").empty()) return fail("не указан path");
                fs::path p = resolve(a.getString("path"));
                if (fs::is_directory(p)) return fail(rel(p) + " — это директория");
                bool existed = fs::exists(p);
                if (ctx.session) ctx.session->snapshot(p);
                if (!writeFile(p, a.getString("content"))) return fail("не удалось записать " + rel(p));
                size_t n = split(a.getString("content"), '\n').size();
                return {true, (existed ? "Перезаписан " : "Создан ") + rel(p) + " (" + countOf((long long)n, "строка", "строки", "строк") + ")", rel(p) + " · " + countOf((long long)n, "строка", "строки", "строк")};
            }});

        tools_.push_back({"edit", "Точечная замена текста в файле. old_string должен совпадать дословно и быть уникальным (или replace_all=true).",
            schema({{"path", "string", "Путь к файлу", true}, {"old_string", "string", "Заменяемый текст", true}, {"new_string", "string", "Новый текст", true}, {"replace_all", "boolean", "Заменить все вхождения", false}}), "edit",
            [](const Json& a, ToolContext& ctx) -> ToolResult {
                fs::path p = resolve(a.getString("path"));
                if (!fs::exists(p)) return fail("файл не найден: " + rel(p));
                std::string c = readFile(p);
                auto r = applyEdit(p, a.getString("old_string"), a.getString("new_string"), a.getBool("replace_all"), c);
                if (!r.ok) return r;
                if (ctx.session) ctx.session->snapshot(p);
                if (!writeFile(p, c)) return fail("не удалось записать " + rel(p));
                return {true, "Изменён " + rel(p) + ": " + r.output, rel(p) + " · " + r.output};
            }});

        tools_.push_back({"multiedit", "Несколько последовательных замен в одном файле атомарно. edits: [{old_string,new_string,replace_all?}]",
            schema({{"path", "string", "Путь к файлу", true}, {"edits", "array", "Список правок", true}}), "edit",
            [](const Json& a, ToolContext& ctx) -> ToolResult {
                fs::path p = resolve(a.getString("path"));
                if (!fs::exists(p)) return fail("файл не найден: " + rel(p));
                std::string c = readFile(p); int k = 0;
                for (auto& e : a.get("edits").arr()) {
                    auto r = applyEdit(p, e.getString("old_string"), e.getString("new_string"), e.getBool("replace_all"), c);
                    if (!r.ok) return fail("правка #" + std::to_string(k + 1) + ": " + r.output);
                    ++k;
                }
                if (k == 0) return fail("список edits пуст");
                if (ctx.session) ctx.session->snapshot(p);
                if (!writeFile(p, c)) return fail("не удалось записать " + rel(p));
                return {true, "Применено правок: " + std::to_string(k) + " в " + rel(p), rel(p) + " · " + countOf((long long)k, "правка", "правки", "правок")};
            }});

        tools_.push_back({"list", "Показать дерево директории (игнорирует .git, node_modules и т.п.).",
            schema({{"path", "string", "Директория (по умолчанию .)", false}, {"depth", "integer", "Глубина (по умолчанию 2)", false}}), "",
            [](const Json& a, ToolContext&) -> ToolResult {
                fs::path root = resolve(a.getString("path", "."));
                int depth = (int)a.getNum("depth", 2); std::ostringstream o; int count = 0;
                std::function<void(const fs::path&, int, const std::string&)> rec = [&](const fs::path& d, int lvl, const std::string& pre) {
                    std::vector<fs::directory_entry> es; std::error_code ec;
                    for (auto& e : fs::directory_iterator(d, ec)) es.push_back(e);
                    std::sort(es.begin(), es.end(), [](auto& x, auto& y) {
                        bool dx = x.is_directory(), dy = y.is_directory();
                        return dx != dy ? dx : x.path().filename() < y.path().filename(); });
                    for (size_t i = 0; i < es.size() && count < 400; ++i) {
                        std::string n = es[i].path().filename().string();
                        bool last = i + 1 == es.size(), dir = es[i].is_directory();
                        o << pre << (last ? "└── " : "├── ") << n << (dir ? "/" : "") << "\n"; ++count;
                        if (dir && lvl < depth && !isIgnoredDir(n)) rec(es[i].path(), lvl + 1, pre + (last ? "    " : "│   "));
                    }
                };
                if (!fs::is_directory(root)) return fail("не директория: " + rel(root));
                o << rel(root) << "/\n"; rec(root, 1, "");
                return {true, o.str(), rel(root) + " · " + countOf((long long)count, "элемент", "элемента", "элементов")};
            }});

        tools_.push_back({"glob", "Найти файлы по glob-шаблону, например **/*.cpp или src/**/*.{ts,tsx}.",
            schema({{"pattern", "string", "Glob-шаблон", true}, {"path", "string", "Корень поиска", false}}), "",
            [](const Json& a, ToolContext&) -> ToolResult {
                fs::path root = resolve(a.getString("path", "."));
                std::regex re = globToRegex(a.getString("pattern"));
                std::vector<std::string> found;
                walk(root, [&](const fs::path& p) {
                    std::error_code ec; std::string r = fs::relative(p, root, ec).generic_string();
                    if (std::regex_match(r, re)) found.push_back(r);
                    return found.size() < 500;
                });
                std::sort(found.begin(), found.end());
                return {true, found.empty() ? "Ничего не найдено" : joinStr(found, "\n"), countOf((long long)found.size(), "файл", "файла", "файлов")};
            }});

        tools_.push_back({"grep", "Поиск по содержимому файлов (ECMAScript regex). include — glob-фильтр файлов.",
            schema({{"pattern", "string", "Регулярное выражение", true}, {"path", "string", "Корень поиска", false}, {"include", "string", "Фильтр файлов, напр. *.cpp", false}}), "",
            [](const Json& a, ToolContext&) -> ToolResult {
                fs::path root = resolve(a.getString("path", "."));
                std::regex re;
                try { re = std::regex(a.getString("pattern"), std::regex::ECMAScript | std::regex::icase); }
                catch (const std::regex_error&) { return fail("некорректное регулярное выражение: " + a.getString("pattern")); }
                bool hasInc = !a.getString("include").empty();
                std::regex inc = hasInc ? globToRegex(a.getString("include")) : std::regex(".*");
                std::ostringstream o; int hits = 0, files = 0;
                walk(root, [&](const fs::path& p) {
                    if (hasInc && !std::regex_match(p.filename().string(), inc)) {
                        std::error_code ec; if (!std::regex_match(fs::relative(p, root, ec).generic_string(), inc)) return true;
                    }
                    std::error_code ec; if (fs::file_size(p, ec) > 2 * 1024 * 1024) return true;
                    std::string c = readFile(p); if (isBinaryContent(c)) return true;
                    auto lines = split(c, '\n'); bool any = false;
                    for (size_t i = 0; i < lines.size(); ++i) if (lines[i].size() <= 4000 && std::regex_search(lines[i], re)) {
                        if (!any) { ++files; any = true; }
                        o << rel(p) << ":" << i + 1 << ": " << truncateUtf8(trim(lines[i]), 220) << "\n";
                        if (++hits >= 300) return false;
                    }
                    return true;
                });
                return {true, hits ? o.str() : "Совпадений нет", countOf((long long)hits, "совпадение", "совпадения", "совпадений") + " в " + countOf((long long)files, "файле", "файлах", "файлах")};
            }});

        tools_.push_back({"bash", "Выполнить shell-команду в директории проекта (сборка, тесты, git и т.д.). Вывод stdout+stderr.",
            schema({{"command", "string", "Команда", true}, {"description", "string", "Кратко: что делает команда", false},
                    {"timeout", "integer", "Таймаут в секундах (по умолчанию 120, максимум 600). Не запускай серверы без &", false}}), "bash",
            [](const Json& a, ToolContext& ctx) -> ToolResult {
                std::string cmd = a.getString("command");
                if (trim(cmd).empty()) return fail("пустая команда");
                double tsec = a.getNum("timeout", 120); if (tsec < 1) tsec = 120; if (tsec > 600) tsec = 600;
                auto r = runCommand(cmd, 60000, ctx.onOutput, (long long)(tsec * 1000));
                std::string out = r.output.empty() ? "(нет вывода)" : r.output;
                if (r.timedOut) return {false, out + "\n[остановлено: превышен таймаут " + std::to_string((int)tsec) + " с]", truncateUtf8(cmd, 60) + " · таймаут"};
                if (r.cancelled) return {false, out + "\n[остановлено пользователем]", truncateUtf8(cmd, 60) + " · прервано"};
                return {r.code == 0, out + "\n[exit code " + std::to_string(r.code) + "]", truncateUtf8(cmd, 60) + " · exit " + std::to_string(r.code)};
            }});

        tools_.push_back({"webfetch", "Загрузить веб-страницу и вернуть её текст (HTML очищается).",
            schema({{"url", "string", "URL (http/https)", true}}), "webfetch",
            [](const Json& a, ToolContext&) -> ToolResult {
                std::string url = a.getString("url");
                if (!startsWith(url, "http://") && !startsWith(url, "https://")) return fail("нужен http(s) URL");
                auto r = runCommand("curl -sSLf --max-time 30 -A " + shellQuote("NovaForge/" + std::string(VERSION)) + " " + shellQuote(url), 2000000);
                if (r.code != 0) return fail("curl завершился с кодом " + std::to_string(r.code) + ": " + truncateUtf8(r.output, 300));
                std::string head = lower(r.output.substr(0, 2000));
                std::string t = head.find("<html") != std::string::npos || head.find("<!doctype html") != std::string::npos ? stripHtml(r.output) : r.output;
                return {true, truncateUtf8(t, 30000), url};
            }});

        tools_.push_back({"todowrite", "Обновить список задач сессии. todos: [{content, status: pending|in_progress|completed, priority?}]",
            schema({{"todos", "array", "Полный список задач", true}}), "",
            [](const Json& a, ToolContext& ctx) -> ToolResult {
                if (!ctx.session) return fail("нет сессии");
                if (!a.get("todos").isArray()) return fail("todos должен быть массивом");
                ctx.session->todos = a.get("todos");
                int done = 0; std::ostringstream o;
                for (auto& t : ctx.session->todos.arr()) {
                    std::string st = t.getString("status", "pending"); if (st == "completed") ++done;
                    o << (st == "completed" ? "[x] " : st == "in_progress" ? "[~] " : "[ ] ") << t.getString("content") << "\n";
                }
                return {true, o.str(), std::to_string(done) + "/" + std::to_string(ctx.session->todos.size()) + " выполнено"};
            }});

        tools_.push_back({"todoread", "Прочитать текущий список задач сессии.", schema({}), "",
            [](const Json&, ToolContext& ctx) -> ToolResult {
                if (!ctx.session || ctx.session->todos.size() == 0) return {true, "Список задач пуст", "0 задач"};
                return {true, ctx.session->todos.dump(2), std::to_string(ctx.session->todos.size()) + " задач"};
            }});
    }
};

} // namespace nova
