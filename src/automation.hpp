// NovaForge — automation: multi-step AI workflows, triggers (manual/interval/watch), webhooks
#pragma once
#include "engine.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <set>

namespace nova {

struct WorkflowRun {
    bool ok = true; std::string output, error; Json steps = Json::array(); long long ms = 0;
};

class Workflows {
public:
    static fs::path projectDir() { return fs::current_path() / ".novaforge" / "workflows"; }
    static fs::path globalDir() { return novaHome() / "workflows"; }

    static std::vector<std::pair<std::string, Json>> list() {
        std::vector<std::pair<std::string, Json>> out;
        for (auto& d : {globalDir(), projectDir()}) {
            std::error_code ec; if (!fs::exists(d, ec)) continue;
            for (auto& e : fs::directory_iterator(d, ec)) {
                if (e.path().extension() != ".json") continue;
                Json j; if (!Json::tryParse(readFile(e.path()), j)) continue;
                std::string n = j.getString("name", e.path().stem().string());
                bool replaced = false;
                for (auto& x : out) if (x.first == n) { x.second = j; replaced = true; }
                if (!replaced) out.push_back({n, j});
            }
        }
        return out;
    }
    static bool find(const std::string& name, Json& out) {
        for (auto& w : list()) if (w.first == name) { out = w.second; return true; }
        return false;
    }
    static bool validName(const std::string& n) { return safeName(n); }
    static fs::path save(const Json& wf) {
        std::string n = wf.getString("name", "workflow");
        if (!validName(n)) throw std::runtime_error("некорректное имя воркфлоу: латиница, цифры, '-', '_' и '.'");
        fs::path p = projectDir() / (n + ".json");
        if (!writeFile(p, wf.dump(2))) throw std::runtime_error("не удалось записать " + p.string());
        return p;
    }

    static Json templateWorkflow(const std::string& name) {
        Json wf = Json::parse(R"({
  "name": "",
  "description": "Ревью изменений → исправления → отчёт в вебхук",
  "trigger": { "type": "manual" },
  "steps": [
    { "id": "diff", "type": "shell", "run": "git diff --stat HEAD || echo 'нет git'" },
    { "id": "review", "type": "ai", "mode": "plan",
      "prompt": "Проведи код-ревью изменений проекта. Каждую находку помечай [CRITICAL], [WARN] или [INFO] с файлом:строкой. Статистика diff:\n{{steps.diff}}\nДополнительно: {{input}}" },
    { "id": "fix", "type": "ai", "condition": "contains:CRITICAL",
      "prompt": "Исправь все CRITICAL-находки из ревью:\n{{steps.review}}" },
    { "id": "notify", "type": "http", "method": "POST", "url": "{{env.NOVA_WEBHOOK_URL}}",
      "body": { "text": "NovaForge: воркфлоу {{workflow}} завершён" }, "condition": "env:NOVA_WEBHOOK_URL" }
  ]
})");
        wf["name"] = name; return wf;
    }

    static std::string render(const std::string& tpl, const std::map<std::string, std::string>& vars) {
        std::string out; size_t i = 0;
        while (i < tpl.size()) {
            size_t a = tpl.find("{{", i);
            if (a == std::string::npos) { out += tpl.substr(i); break; }
            size_t b = tpl.find("}}", a);
            if (b == std::string::npos) { out += tpl.substr(i); break; }
            out += tpl.substr(i, a - i);
            std::string key = trim(tpl.substr(a + 2, b - a - 2));
            if (startsWith(key, "env.")) out += env(key.substr(4).c_str());
            else { auto it = vars.find(key); if (it != vars.end()) out += it->second; }
            i = b + 2;
        }
        return out;
    }
    static Json renderJson(const Json& j, const std::map<std::string, std::string>& vars) {
        if (j.isString()) return Json(render(j.str(), vars));
        if (j.isArray()) { Json a = Json::array(); for (auto& x : j.arr()) a.push(renderJson(x, vars)); return a; }
        if (j.isObject()) { Json o = Json::object(); for (auto& kv : j.obj()) o[kv.first] = renderJson(kv.second, vars); return o; }
        return j;
    }

    static bool checkCondition(const std::string& cond, const std::string& prev, bool prevOk) {
        if (cond.empty() || cond == "always") return true;
        if (cond == "success") return prevOk;
        if (cond == "failure") return !prevOk;
        if (startsWith(cond, "contains:")) return prev.find(cond.substr(9)) != std::string::npos;
        if (startsWith(cond, "not_contains:")) return prev.find(cond.substr(13)) == std::string::npos;
        if (startsWith(cond, "env:")) return !env(cond.substr(4).c_str()).empty();
        return true;
    }

    // log(line) is called for progress output
    static WorkflowRun run(const Json& wf, const std::string& input, bool autoApprove,
                           const std::function<void(const std::string&)>& log, EngineEvents* evOverride = nullptr) {
        WorkflowRun wr; long long t0 = nowMs();
        std::map<std::string, std::string> vars;
        vars["input"] = input; vars["date"] = isoTime(); vars["cwd"] = fs::current_path().string();
        vars["workflow"] = wf.getString("name"); vars["prev"] = "";
        bool prevOk = true;
        Session ses = Session::create(Config::get().model()); ses.title = "workflow: " + wf.getString("name");
        size_t total = wf.get("steps").size();
        int idx = 0;
        for (auto& step : wf.get("steps").arr()) {
            ++idx;
            std::string id = step.getString("id", "step" + std::to_string(idx));
            std::string type = step.getString("type", step.has("run") ? "shell" : step.has("url") ? "http" : "ai");
            if (type == "agent") type = "ai";   // workflows of older versions
            Json sj = Json::object(); sj["id"] = id; sj["type"] = type;
            if (!checkCondition(step.getString("condition"), vars["prev"], prevOk)) {
                if (log) log("skip " + id + "  (условие " + step.getString("condition") + " не выполнено)");
                sj["skipped"] = true; wr.steps.push(sj); continue;
            }
            if (log) log("step " + std::to_string(idx) + "/" + std::to_string(total) + "  " + id + "  (" + type + ")");
            long long s0 = nowMs(); std::string out; bool ok = true;

            if (type == "shell") {
                auto r = runCommand(render(step.getString("run"), vars), 100000);
                out = r.output; ok = r.code == 0;
            } else if (type == "http") {
                std::string url = render(step.getString("url"), vars);
                std::string method = step.getString("method", "POST");
                for (auto& ch : method) ch = (char)toupper((unsigned char)ch);
                static const std::set<std::string> methods = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
                if (url.empty()) { out = "пустой url"; ok = false; }
                else if (!methods.count(method)) { out = "неподдерживаемый метод: " + method; ok = false; }
                else {
                    std::string cmd = "curl -sSf --max-time 60 -X " + method + " " + shellQuote(url);
                    for (auto& h : step.get("headers").obj())
                        if (h.second.isString()) cmd += " -H " + shellQuote(h.first + ": " + render(h.second.str(), vars));
                    if (step.has("body")) {
                        Json b = renderJson(step.get("body"), vars);
                        cmd += " -H " + shellQuote("Content-Type: application/json") + " --data-binary " + shellQuote(b.isString() ? b.str() : b.dump());
                    }
                    auto r = runCommand(cmd, 100000); out = r.output; ok = r.code == 0;
                }
            } else {
                // "mode": "plan" — read-only (analysis, review); otherwise full access. In old workflows
                // "agent": "plan" means the same, other agent names are ignored.
                Mode mode = Mode::byName(step.getString("mode", step.getString("agent")));
                if (step.has("model")) ses.model = step.getString("model");
                EngineEvents ev;
                if (evOverride) ev = *evOverride;
                else ev.onToolStart = [&](const std::string& n, const Json& a) {
                    if (log) log("    " + n + "  " + truncateUtf8(a.getString("path", a.getString("command", a.getString("pattern", a.getString("url")))), 70));
                };
                ev.askPermission = [autoApprove](const std::string&, const std::string&, const std::string&) { return autoApprove; };
                if (!step.getBool("shared_context", false)) ses.messages = Json::array();
                ses.beginTurn();
                auto r = Engine::run(ses, mode, render(step.getString("prompt"), vars), ev);
                ses.endTurn();
                out = r.ok ? r.text : r.error; ok = r.ok;
            }
            if (step.has("save_to")) writeFile(render(step.getString("save_to"), vars), out);
            vars["steps." + id] = trim(out); vars["prev"] = trim(out); prevOk = ok;
            sj["ok"] = ok; sj["ms"] = nowMs() - s0; sj["output"] = truncateUtf8(out, 4000);
            wr.steps.push(sj);
            if (log) log(std::string(ok ? "  ok   " : "  fail ") + id + "  " + fmtDuration(nowMs() - s0));
            if (!ok && !step.getBool("continue_on_error", false)) { wr.ok = false; wr.error = "шаг " + id + " завершился с ошибкой"; break; }
        }
        ses.save();
        wr.output = vars["prev"]; wr.ms = nowMs() - t0;
        Json logJ = Json::object(); logJ["workflow"] = wf.getString("name"); logJ["ok"] = wr.ok; logJ["ms"] = wr.ms;
        logJ["input"] = input; logJ["steps"] = wr.steps; logJ["time"] = isoTime(); logJ["session"] = ses.id;
        std::string logName = validName(wf.getString("name")) ? wf.getString("name") : "workflow";
        writeFile(novaHome() / "runs" / (logName + "-" + std::to_string(nowMs()) + ".json"), logJ.dump(2));
        return wr;
    }
};

// Background scheduler for interval / watch triggers
class Scheduler {
public:
    static Scheduler& get() { static Scheduler s; return s; }
    std::function<void(const std::string&)> log;
    bool autoApprove = false;

    void start() {
        if (running_) return;
        running_ = true;
        th_ = std::thread([this] { loop(); });
    }
    void stop() { running_ = false; if (th_.joinable()) th_.join(); }
    size_t activeCount() const { return active_; }

private:
    std::atomic<bool> running_{false}; std::thread th_; std::atomic<size_t> active_{0};
    std::map<std::string, long long> lastRun_;
    std::map<std::string, long long> lastSig_;
    std::mutex runMu_;

    static long long signature(const Json& paths) {
        long long sig = 0;
        for (auto& p : paths.arr()) {
            if (!p.isString()) continue;
            std::error_code ec; fs::path root = p.str();
            if (fs::is_regular_file(root, ec)) { sig ^= (long long)fs::last_write_time(root, ec).time_since_epoch().count(); continue; }
            detail::walk(root, [&](const fs::path& f) {
                std::error_code e2; sig = sig * 31 + (long long)fs::last_write_time(f, e2).time_since_epoch().count() + (long long)f.string().size();
                return true; });
        }
        return sig;
    }

    void loop() {
        while (running_) {
            size_t act = 0;
            for (auto& [name, wf] : Workflows::list()) {
                const Json& tr = wf.get("trigger"); std::string type = tr.getString("type", "manual");
                if (type == "manual") continue;
                ++act;
                bool fire = false; long long now = nowMs();
                if (type == "interval") {
                    long long every = (long long)tr.getNum("seconds", 3600) * 1000;
                    if (!lastRun_.count(name)) lastRun_[name] = tr.getBool("run_on_start", false) ? 0 : now;
                    if (now - lastRun_[name] >= every) fire = true;
                } else if (type == "watch") {
                    Json paths = tr.get("paths").isArray() ? tr.get("paths") : Json::array().push(".");
                    long long sig = signature(paths);
                    if (!lastSig_.count(name)) lastSig_[name] = sig;
                    else if (sig != lastSig_[name] && now - lastRun_[name] > (long long)tr.getNum("debounce", 3) * 1000) { fire = true; lastSig_[name] = sig; }
                }
                if (fire) {
                    lastRun_[name] = now;
                    std::lock_guard<std::mutex> g(runMu_);
                    if (log) log("trigger " + type + " -> " + name);
                    try {
                        auto r = Workflows::run(wf, tr.getString("input", "автозапуск по триггеру " + type), autoApprove, log);
                        if (log) log(name + (r.ok ? " завершён" : " завершился с ошибкой") + " за " + fmtDuration(r.ms));
                    } catch (const std::exception& e) { if (log) log(name + ": ошибка: " + e.what()); }
                    if (type == "watch") lastSig_[name] = signature(tr.get("paths").isArray() ? tr.get("paths") : Json::array().push("."));
                }
            }
            active_ = act;
            for (int i = 0; i < 20 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

} // namespace nova
