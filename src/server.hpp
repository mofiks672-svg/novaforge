// NovaForge — headless HTTP REST API (JSON + SSE streaming) for scripts and integrations
#pragma once
#include "automation.hpp"
#include <thread>
#include <mutex>
#include <set>

namespace nova {

struct HttpRequest {
    std::string method, path, query, body;
    std::map<std::string, std::string> headers;
    std::string header(const std::string& k) const { auto it = headers.find(lower(k)); return it == headers.end() ? "" : it->second; }
    std::string param(const std::string& k) const {
        for (auto& kv : split(query, '&')) { auto p = kv.find('='); if (p != std::string::npos && kv.substr(0, p) == k) return kv.substr(p + 1); }
        return "";
    }
};

struct BadRequest : std::runtime_error { using std::runtime_error::runtime_error; };

class Server {
public:
    int port = 4096; std::string host = "127.0.0.1"; std::string apiKey; bool autoApprove = false;

    int run() {
        net::initSockets();
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
        std::string bindHost = host == "localhost" ? "127.0.0.1" : host;
        if (inet_pton(AF_INET, bindHost.c_str(), &addr.sin_addr) != 1) {
            UI::get().error("некорректный адрес --host: " + host + " (нужен IPv4, например 127.0.0.1 или 0.0.0.0)"); return 1;
        }
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
        if (bind(s, (sockaddr*)&addr, sizeof addr) < 0) { UI::get().error("не удалось занять " + host + ":" + std::to_string(port)); NOVA_CLOSE(s); return 1; }
        listen(s, 64);
        auto& ui = UI::get();
        std::string base = "http://" + host + ":" + std::to_string(port);
        std::cout << ui.bold("nova serve") << ui.muted("  " + std::string(VERSION)) << "\n";
        ui.keyValues("", {
            {"api", base + "/api"},
            {"auth", apiKey.empty() ? ui.warn("выключена (задайте server.api_key или NOVA_SERVER_KEY)") : "Bearer-токен"},
            {"permissions", autoApprove ? ui.warn("авто-одобрение (--yes)") : "ask → deny"},
            {"workflows", std::to_string(Workflows::list().size()) + ", планировщик запущен"},
        });
        if (apiKey.empty() && bindHost != "127.0.0.1") ui.warning("сервер доступен по сети без авторизации");
        std::cout << "\n" << ui.muted("Ctrl+C — остановить") << "\n\n";
        Scheduler::get().autoApprove = autoApprove;
        Scheduler::get().log = [](const std::string& l) { std::cout << UI::get().muted(isoTime().substr(11)) << "  " << l << std::endl; };
        Scheduler::get().start();
        while (true) {
            sockaddr_in cli{}; socklen_t len = sizeof cli;
            sock_t c = accept(s, (sockaddr*)&cli, &len);
#ifdef _WIN32
            if (c == INVALID_SOCKET) continue;
#else
            if (c < 0) continue;
#endif
            std::thread([this, c] { handle(c); }).detach();
        }
    }

private:
    std::mutex sesMu_; std::set<std::string> busy_;

    static void sendAll(sock_t c, const std::string& d) {
        size_t off = 0;
        while (off < d.size()) { auto n = send(c, d.data() + off, (int)(d.size() - off), 0); if (n <= 0) return; off += (size_t)n; }
    }
    static std::string statusText(int code) {
        switch (code) { case 200: return "OK"; case 201: return "Created"; case 204: return "No Content"; case 400: return "Bad Request";
            case 401: return "Unauthorized"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 409: return "Conflict"; default: return "Internal Server Error"; }
    }
    static std::string cors() {
        return "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Authorization, Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
    }
    static void respond(sock_t c, int code, const std::string& body, const std::string& ctype = "application/json; charset=utf-8") {
        std::string h = "HTTP/1.1 " + std::to_string(code) + " " + statusText(code) + "\r\nContent-Type: " + ctype +
                        "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + cors() + "Connection: close\r\n\r\n";
        sendAll(c, h + body);
    }
    static void json(sock_t c, int code, const Json& j) { respond(c, code, j.dump(2)); }
    static void error(sock_t c, int code, const std::string& m) { json(c, code, Json::object().set("error", m)); }

    bool readRequest(sock_t c, HttpRequest& r) {
        std::string data; char buf[8192];
        while (data.find("\r\n\r\n") == std::string::npos) {
            auto n = recv(c, buf, sizeof buf, 0); if (n <= 0) return false;
            data.append(buf, (size_t)n); if (data.size() > 1 << 20) return false;
        }
        size_t he = data.find("\r\n\r\n");
        auto lines = split(data.substr(0, he), '\n');
        auto first = split(trim(lines[0]), ' ');
        if (first.size() < 2) return false;
        r.method = first[0]; std::string target = first[1];
        size_t q = target.find('?'); r.path = target.substr(0, q); if (q != std::string::npos) r.query = target.substr(q + 1);
        for (size_t i = 1; i < lines.size(); ++i) {
            size_t p = lines[i].find(':'); if (p == std::string::npos) continue;
            r.headers[lower(trim(lines[i].substr(0, p)))] = trim(lines[i].substr(p + 1));
        }
        r.body = data.substr(he + 4);
        size_t cl = 0; try { cl = std::stoul(r.header("content-length").empty() ? "0" : r.header("content-length")); } catch (...) {}
        if (cl > 32 * 1024 * 1024) return false;
        while (r.body.size() < cl) { auto n = recv(c, buf, sizeof buf, 0); if (n <= 0) break; r.body.append(buf, (size_t)n); }
        return true;
    }

    bool authorized(const HttpRequest& r) {
        if (apiKey.empty()) return true;
        std::string a = r.header("authorization");
        return a == "Bearer " + apiKey || r.param("key") == apiKey;
    }

    void handle(sock_t c) {
        HttpRequest r;
        if (!readRequest(c, r)) { NOVA_CLOSE(c); return; }
        std::cout << UI::get().muted(isoTime().substr(11)) << "  " << r.method << " " << r.path << std::endl;
        try { route(c, r); }
        catch (const BadRequest& e) { error(c, 400, e.what()); }
        catch (const std::exception& e) { error(c, 500, e.what()); }
        NOVA_CLOSE(c);
    }

    Json bodyJson(const HttpRequest& r) {
        Json j; if (r.body.empty()) return Json::object();
        if (!Json::tryParse(r.body, j) || !j.isObject()) throw BadRequest("тело запроса должно быть JSON-объектом");
        return j;
    }

    EngineEvents headlessEvents() {
        EngineEvents ev; bool aa = autoApprove;
        ev.askPermission = [aa](const std::string&, const std::string&, const std::string&) { return aa; };
        return ev;
    }

    bool lock(const std::string& id) { std::lock_guard<std::mutex> g(sesMu_); if (busy_.count(id)) return false; busy_.insert(id); return true; }
    void unlock(const std::string& id) { std::lock_guard<std::mutex> g(sesMu_); busy_.erase(id); }
    // Releases the session lock even if the engine throws.
    struct SessionLock { Server* s; std::string id; ~SessionLock() { s->unlock(id); } };

    void route(sock_t c, const HttpRequest& r) {
        const std::string& m = r.method; auto parts = split(r.path, '/'); // "", "api", ...
        if (m == "OPTIONS") { respond(c, 204, ""); return; }
        if (r.path == "/") { json(c, 200, Json::object().set("name", "NovaForge").set("version", VERSION).set("api", "/api")); return; }
        if (r.path == "/api/health") { json(c, 200, Json::object().set("status", "ok").set("version", VERSION)); return; }
        if (!startsWith(r.path, "/api/")) { error(c, 404, "not found"); return; }
        if (!authorized(r)) { error(c, 401, "неверный или отсутствующий API-ключ (Authorization: Bearer <key>)"); return; }
        std::string res = parts.size() > 2 ? parts[2] : "";
        std::string id = parts.size() > 3 ? parts[3] : "";
        std::string sub = parts.size() > 4 ? parts[4] : "";

        if (res == "info" && m == "GET") {
            Json j = Json::object(); j["name"] = "NovaForge"; j["version"] = VERSION; j["model"] = Config::get().model();
            j["cwd"] = fs::current_path().string();
            j["workflows"] = Workflows::list().size(); j["tools"] = ToolRegistry::get().all().size();
            json(c, 200, j); return;
        }
        if (res == "tools" && m == "GET") {
            Json a = Json::array();
            for (auto& t : ToolRegistry::get().all()) a.push(Json::object().set("name", t.name).set("description", t.description).set("permission", t.permGroup).set("parameters", t.parameters));
            json(c, 200, a); return;
        }
        if (res == "models" && m == "GET") {
            Json a = Json::array();
            for (auto& p : Config::get().providers()) {
                bool conn = !p.needsKey || !Config::get().apiKey(p).empty();
                std::vector<std::string> ms = p.models;
                if (p.local() && !p.needsKey) { auto info = LLM::localInfo(p); if (!info.up) continue; ms = info.models; }
                for (auto& md : ms) a.push(Json::object().set("id", p.id + "/" + md).set("provider", p.name).set("connected", conn));
            }
            json(c, 200, a); return;
        }
        if (res == "sessions") {
            if (m == "GET" && id.empty()) { Json a = Json::array(); for (auto& s : Session::list(100)) a.push(s.summaryJson()); json(c, 200, a); return; }
            if (m == "POST" && id.empty()) {
                Json b = bodyJson(r);
                Session s = Session::create(b.getString("model", Config::get().model()));
                if (b.has("title")) s.title = b.getString("title");
                s.save(); json(c, 201, s.summaryJson()); return;
            }
            Session s; if (!Session::load(id, s)) { error(c, 404, "сессия не найдена"); return; }
            if (m == "GET" && sub.empty()) { json(c, 200, s.toJson()); return; }
            if (m == "GET" && sub == "export") { respond(c, 200, s.exportMarkdown(), "text/markdown; charset=utf-8"); return; }
            if (m == "DELETE") { Session::remove(id); json(c, 200, Json::object().set("deleted", id)); return; }
            if (m == "POST" && (sub == "messages" || sub == "stream")) {
                Json b = bodyJson(r); std::string text = b.getString("text");
                if (trim(text).empty()) { error(c, 400, "text обязателен"); return; }
                if (b.has("model")) s.model = b.getString("model");
                Mode mode = Mode::byName(b.getString("mode"));
                if (!lock(id)) { error(c, 409, "сессия уже выполняет запрос"); return; }
                SessionLock guard{this, id};
                EngineEvents ev = headlessEvents();
                if (sub == "stream") {
                    sendAll(c, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n" + cors() + "Connection: close\r\n\r\n");
                    auto emit = [c](const Json& e) { sendAll(c, "data: " + e.dump() + "\n\n"); };
                    ev.onText = [&](const std::string& d) { emit(Json::object().set("type", "text").set("delta", d)); };
                    ev.onToolStart = [&](const std::string& n, const Json& a) { emit(Json::object().set("type", "tool_start").set("tool", n).set("args", a)); };
                    ev.onToolEnd = [&](const std::string& n, const ToolResult& t) { emit(Json::object().set("type", "tool_end").set("tool", n).set("ok", t.ok).set("title", t.title).set("output", truncateUtf8(t.output, 2000))); };
                    ev.onResponseEnd = [&]() { emit(Json::object().set("type", "step_end")); };
                    s.beginTurn(); auto rr = Engine::run(s, mode, text, ev); s.endTurn(); s.save();
                    emit(Json::object().set("type", "done").set("ok", rr.ok).set("error", rr.error).set("steps", rr.steps).set("title", s.title)
                        .set("tokens_in", s.tokensIn).set("tokens_out", s.tokensOut));
                    return;
                }
                s.beginTurn(); auto rr = Engine::run(s, mode, text, ev); s.endTurn(); s.save();
                Json j = Json::object(); j["ok"] = rr.ok; j["reply"] = rr.text; j["error"] = rr.error; j["steps"] = rr.steps; j["tools"] = rr.toolLog;
                j["session"] = s.summaryJson(); json(c, rr.ok ? 200 : 500, j); return;
            }
        }
        if (res == "run" && m == "POST") {
            Json b = bodyJson(r);
            if (trim(b.getString("prompt")).empty()) { error(c, 400, "prompt обязателен"); return; }
            Session s = Session::create(b.getString("model", Config::get().model()));
            EngineEvents ev = headlessEvents();
            s.beginTurn(); auto rr = Engine::run(s, Mode::byName(b.getString("mode")), b.getString("prompt"), ev); s.endTurn(); s.save();
            Json j = Json::object(); j["ok"] = rr.ok; j["reply"] = rr.text; j["error"] = rr.error; j["steps"] = rr.steps; j["tools"] = rr.toolLog; j["session_id"] = s.id;
            json(c, rr.ok ? 200 : 500, j); return;
        }
        if (res == "workflows") {
            if (m == "GET" && id.empty()) { Json a = Json::array(); for (auto& w : Workflows::list()) a.push(w.second); json(c, 200, a); return; }
            if (m == "POST" && id.empty()) {
                Json b = bodyJson(r); if (!Workflows::validName(b.getString("name")) || !b.get("steps").isArray()) { error(c, 400, "нужны name (латиница, цифры, - _ .) и steps[]"); return; }
                json(c, 201, Json::object().set("saved", Workflows::save(b).string())); return;
            }
            if (m == "POST" && sub == "run") {
                Json wf; if (!Workflows::find(id, wf)) { error(c, 404, "воркфлоу не найден"); return; }
                Json b = bodyJson(r);
                auto wr = Workflows::run(wf, b.getString("input"), autoApprove || b.getBool("approve"), nullptr);
                Json j = Json::object(); j["ok"] = wr.ok; j["output"] = wr.output; j["error"] = wr.error; j["steps"] = wr.steps; j["ms"] = wr.ms;
                json(c, wr.ok ? 200 : 500, j); return;
            }
        }
        if (res == "runs" && m == "GET") {
            std::vector<fs::path> files; std::error_code ec;
            for (auto& e : fs::directory_iterator(novaHome() / "runs", ec)) files.push_back(e.path());
            std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { std::error_code e; return fs::last_write_time(a, e) > fs::last_write_time(b, e); });
            Json a = Json::array();
            for (size_t i = 0; i < files.size() && i < 30; ++i) { Json j; if (Json::tryParse(readFile(files[i]), j)) a.push(j); }
            json(c, 200, a); return;
        }
        error(c, 404, "маршрут не найден: " + m + " " + r.path);
    }
};

} // namespace nova
