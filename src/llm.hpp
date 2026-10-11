// NovaForge — LLM client: OpenAI-compatible APIs (SSE streaming via curl), Ollama native API,
// model lists loaded from the providers, fast probing of local servers.
#pragma once
#include "config.hpp"
#include "net.hpp"
#include "ui.hpp"
#include <map>
#include <thread>
#include <set>
#include <mutex>

namespace nova {

struct ToolCall { std::string id, name, arguments; };

struct LLMRequest {
    std::string model;          // provider/model
    Json messages = Json::array();
    Json tools = Json::array();
    double temperature = -1;
    std::function<void(const std::string&)> onNotice;   // shown right away (model substituted, tools off…)
};

struct LLMResponse {
    std::string content, finishReason, error, notice;
    std::vector<ToolCall> toolCalls;
    long long promptTokens = 0, completionTokens = 0;
    bool cancelled = false;
    bool ok() const { return error.empty(); }
};

using DeltaFn = std::function<void(const std::string&)>;

// Hides <think>…</think> blocks of reasoning models (DeepSeek-R1, Qwen3, …) from the answer.
class ThinkFilter {
public:
    std::string feed(const std::string& d) {
        buf_ += d; std::string out;
        while (!buf_.empty()) {
            const std::string tag = in_ ? "</think>" : "<think>";
            size_t p = buf_.find(tag);
            if (p != std::string::npos) {
                if (!in_) out += buf_.substr(0, p);
                buf_.erase(0, p + tag.size()); in_ = !in_;
                if (!in_) { while (!buf_.empty() && (buf_[0] == '\n' || buf_[0] == '\r')) buf_.erase(0, 1); stripLead_ = true; }
                continue;
            }
            // keep a possible partial tag at the end
            size_t keep = 0;
            for (size_t k = std::min(buf_.size(), tag.size() - 1); k > 0; --k)
                if (buf_.compare(buf_.size() - k, k, tag, 0, k) == 0) { keep = k; break; }
            if (!in_) out += buf_.substr(0, buf_.size() - keep);
            buf_.erase(0, buf_.size() - keep);
            break;
        }
        if (stripLead_) { size_t i = out.find_first_not_of("\r\n"); if (i == std::string::npos) return ""; out.erase(0, i); stripLead_ = false; }
        return out;
    }
    std::string flush() { std::string r = in_ ? "" : buf_; buf_.clear(); return r; }
    bool thinking() const { return in_; }
private:
    std::string buf_; bool in_ = false, stripLead_ = false;
};

class LLM {
public:
    // What a local server (Ollama, LM Studio, llama.cpp) reports: is it running, which models are installed.
    struct LocalInfo { bool up = false; std::vector<std::string> models; std::string detail; long long at = 0; };

    // Cached for a few seconds. A stale or forced refresh probes every local server at once
    // (in parallel, native sockets), so a screen that lists all providers waits at most one short timeout.
    static LocalInfo localInfo(const Provider& p, bool refresh = false) {
        std::string key = p.id + "|" + p.baseURL;
        {
            std::lock_guard<std::mutex> g(cacheMu());
            auto it = cache().find(key);
            if (!refresh && it != cache().end() && nowMs() - it->second.at < 10000) return it->second;
        }
        std::vector<Provider> batch = {p};
        for (auto& q : Config::get().providers())
            if (!q.needsKey && q.local() && q.id != p.id) {
                std::lock_guard<std::mutex> g(cacheMu());
                auto it = cache().find(q.id + "|" + q.baseURL);
                if (it == cache().end() || nowMs() - it->second.at >= 10000) batch.push_back(q);
            }
        probeAll(batch);
        std::lock_guard<std::mutex> g(cacheMu());
        return cache()[key];
    }
    // Probe local servers now (in parallel); used by the boot screen and "refresh" actions.
    static void probeLocal() {
        std::vector<Provider> v; for (auto& q : Config::get().providers()) if (!q.needsKey && q.local()) v.push_back(q);
        probeAll(v);
    }

    // Lower is better: coder / tool-capable families first, small chat-only families last.
    static int modelRank(const std::string& m) {
        std::string l = lower(m);
        static const std::vector<std::pair<const char*, int>> r = {
            {"coder", 0}, {"qwen3", 1}, {"qwen2.5", 1}, {"devstral", 1}, {"gpt-oss", 1}, {"llama3.1", 2}, {"llama3.2", 2}, {"llama3.3", 2},
            {"mistral", 2}, {"command-r", 2}, {"granite", 3}, {"hermes", 3}, {"deepseek-r1", 4}, {"llama3", 5}, {"phi", 6}, {"gemma", 6}};
        for (auto& [k, v] : r) if (l.find(k) != std::string::npos) return v;
        return 4;
    }

    static LLMResponse chat(const LLMRequest& req, const DeltaFn& onDelta = nullptr) {
        auto [pid, mid] = Config::splitModel(req.model);
        LLMResponse res;
        if (trim(req.model).empty()) { res.error = "Модель не выбрана. Подключите API: /connect"; return res; }
        if (pid.empty()) { res.error = "Укажите модель вместе с подключением: провайдер/модель (например, openai/" + mid + "). Список: /models"; return res; }
        Provider prov;
        if (!Config::get().findProvider(pid, prov)) { res.error = "Неизвестное подключение: " + pid + " (см. /connect)"; return res; }
        std::string key = Config::get().apiKey(prov);
        if (prov.needsKey && key.empty()) {
            res.error = "Нет API-ключа для " + prov.name + ". Откройте /connect или выполните `nova auth login " + prov.id + "`" +
                        (prov.envKey.empty() ? "" : " (или задайте " + prov.envKey + ")");
            return res;
        }
        if (prov.local()) {
            LocalInfo info = localInfo(prov);
            if (!info.up) info = localInfo(prov, true);
            if (!info.up) { res.error = prov.name + " не отвечает на " + prov.baseURL + ". " + startHint(prov); return res; }
            if (!info.models.empty() && std::find(info.models.begin(), info.models.end(), mid) == info.models.end()) {
                std::string found;
                for (auto& m : info.models) if (m == mid + ":latest" || startsWith(m, mid + ":") || lower(m) == lower(mid)) { found = m; break; }
                if (found.empty() && (mid.empty() || mid == "local-model" || !prov.ollama())) found = info.models[0];
                if (found.empty()) {
                    res.error = "Модель «" + mid + "» не установлена в " + prov.name + ". Установлены: " + joinStr(info.models, ", ") +
                                ". Выберите: /model " + prov.id + "/" + info.models[0] + (prov.ollama() ? "  или скачайте: ollama pull " + mid : "");
                    return res;
                }
                if (found != mid + ":latest") { res.notice = "модель " + mid + " не найдена, использую " + found; if (req.onNotice) { req.onNotice(res.notice); res.notice.clear(); } }
                mid = found;
            }
        }
        std::string modelKey = prov.id + "/" + mid;
        bool withTools = req.tools.size() > 0 && !noTools().count(modelKey);
        LLMResponse r = prov.ollama() ? ollamaChat(prov, mid, req, withTools, onDelta) : openaiChat(prov, key, mid, req, withTools, onDelta);
        if (!r.ok() && withTools && !r.cancelled && toolsUnsupported(r.error) && r.content.empty()) {
            noTools().insert(modelKey);
            std::string note = "модель " + mid + " не поддерживает инструменты — отвечает без доступа к файлам";
            if (req.onNotice) { req.onNotice(note); note.clear(); }
            r = prov.ollama() ? ollamaChat(prov, mid, req, false, onDelta) : openaiChat(prov, key, mid, req, false, onDelta);
            r.notice = res.notice.empty() ? note : note.empty() ? res.notice : res.notice + "; " + note;
        } else if (!res.notice.empty()) r.notice = res.notice;
        if (!r.ok() && prov.local() && r.error.find("HTTP 0") == 0) r.error = prov.name + " не отвечает на " + prov.baseURL + ". " + startHint(prov);
        return r;
    }

    // Loads the model list from the provider: GET {baseURL}/models (OpenAI-compatible), for Ollama /api/tags.
    // Returns the HTTP status (0 = no connection); detail is a human-readable result.
    static int fetchModels(const Provider& prov, std::vector<std::string>& ids, std::string& detail) {
        ids.clear();
        if (prov.local() && !prov.needsKey) {
            LocalInfo info = localInfo(prov, true);
            ids = info.models;
            detail = info.up ? info.detail : "не запущен. " + startHint(prov);
            return info.up ? 200 : 0;
        }
        std::string body; int code = get(prov, trimSlash(prov.baseURL) + "/models", body, 20, &detail);
        Json j;
        if (code >= 200 && code < 300 && Json::tryParse(body, j)) {
            const Json& list = j.get("data").isArray() ? j.get("data") : j.get("models");   // "models": some Gemini-style APIs
            std::vector<std::pair<double, std::string>> v; bool dated = list.size() > 0;
            for (auto& m : list.arr()) {
                std::string id = m.isString() ? m.str() : m.getString("id", m.getString("name"));
                if (startsWith(id, "models/")) id = id.substr(7);
                if (id.empty()) continue;
                double created = m.getNum("created", 0); if (created <= 0) dated = false;
                v.push_back({created, id});
            }
            // newest first when the API says when models appeared (OpenAI, Groq, Together…), otherwise API order
            if (dated) std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
            std::set<std::string> seen;
            for (auto& x : v) if (seen.insert(x.second).second) ids.push_back(x.second);
            ids = chatModels(ids);
        }
        if (code == 0) { if (detail.empty()) detail = "нет ответа"; }
        else if (code < 300) detail = ids.empty() ? "соединение установлено, но список моделей пуст" : "моделей: " + std::to_string(ids.size());
        else if (code == 401 || code == 403) detail = "ключ не принят (HTTP " + std::to_string(code) + ")";
        else detail = "сервер ответил " + std::to_string(code);
        return code;
    }
    // Checks a connection. Returns the HTTP status (0 = no connection).
    static int ping(const Provider& prov, std::string& detail, std::vector<std::string>* ids = nullptr) {
        std::vector<std::string> tmp; int code = fetchModels(prov, ids ? *ids : tmp, detail);
        return code;
    }
    // Drops models that cannot chat: embeddings, speech, images, moderation, rerankers.
    static std::vector<std::string> chatModels(const std::vector<std::string>& in) {
        static const char* skip[] = {"embed", "bge-", "minilm", "rerank", "whisper", "tts", "dall-e", "moderation", "transcribe",
                                     "gpt-image", "-realtime", "davinci-002", "babbage-002", "text-similarity", "imagen", "veo-", "aqa"};
        std::vector<std::string> out;
        for (auto& m : in) {
            std::string l = lower(m); bool bad = false;
            for (auto* k : skip) if (l.find(k) != std::string::npos) { bad = true; break; }
            if (!bad) out.push_back(m);
        }
        return out;
    }

    static std::string firstLineOf(const std::string& s) { auto p = s.find('\n'); return p == std::string::npos ? s : s.substr(0, p); }

    static std::string startHint(const Provider& p) {
        if (p.ollama()) return "Запустите Ollama (приложение или `ollama serve`) и скачайте модель: `ollama pull qwen2.5-coder:7b`.";
        if (p.id == "lmstudio") return "В LM Studio откройте вкладку Developer, загрузите модель и нажмите Start Server.";
        if (p.id == "llamacpp") return "Запустите сервер: `llama-server -m model.gguf --port 8080 --jinja`.";
        return "Проверьте, что сервер запущен и адрес верный (/connect).";
    }

    // Simple completion without tools (used for titles and summaries)
    static LLMResponse complete(const std::string& model, const std::string& system, const std::string& user, double temp = 0.3) {
        LLMRequest r; r.model = model; r.temperature = temp;
        r.messages.push(Json::object().set("role", "system").set("content", system));
        r.messages.push(Json::object().set("role", "user").set("content", user));
        return chat(r);
    }

private:
    static std::mutex& cacheMu() { static std::mutex m; return m; }
    static std::map<std::string, LocalInfo>& cache() { static std::map<std::string, LocalInfo> c; return c; }
    static void probeAll(const std::vector<Provider>& ps) {
        static std::mutex probing; std::lock_guard<std::mutex> one(probing);
        std::vector<LocalInfo> out(ps.size());
        std::vector<std::thread> th;
        for (size_t i = 0; i < ps.size(); ++i) th.emplace_back([&, i] { out[i] = probe(ps[i]); });
        for (auto& t : th) t.join();
        std::lock_guard<std::mutex> g(cacheMu());
        for (size_t i = 0; i < ps.size(); ++i) cache()[ps[i].id + "|" + ps[i].baseURL] = out[i];
    }
    static LocalInfo probe(const Provider& p) {
        LocalInfo info;
        std::string body, err; int code = 0;
        if (p.ollama()) {
            code = get(p, nativeRoot(p) + "/api/tags", body, 3, &err);
            Json j; if (code == 200 && Json::tryParse(body, j) && j.get("models").isArray()) {
                info.up = true;
                for (auto& m : j.get("models").arr()) if (m.get("name").isString()) info.models.push_back(m.get("name").str());
            }
        }
        if (!info.up && (code > 0 || !p.ollama())) {   // nothing listens on the port: no point in a second request
            code = get(p, trimSlash(p.baseURL) + "/models", body, 3, &err);
            Json j; if (code > 0 && code < 500) info.up = true;
            if (code == 200 && Json::tryParse(body, j))
                for (auto& m : j.get("data").arr()) if (m.get("id").isString()) info.models.push_back(m.get("id").str());
        }
        info.models = chatModels(info.models);
        std::sort(info.models.begin(), info.models.end(), [](const std::string& a, const std::string& b) {
            int ra = modelRank(a), rb = modelRank(b); return ra != rb ? ra < rb : a < b; });
        info.detail = !info.up ? "не запущен" : info.models.empty() ? "запущен, моделей нет" :
                      "запущен · " + std::to_string(info.models.size()) + " " + (info.models.size() % 10 == 1 && info.models.size() % 100 != 11 ? "модель" :
                      info.models.size() % 10 >= 2 && info.models.size() % 10 <= 4 && (info.models.size() % 100 < 12 || info.models.size() % 100 > 14) ? "модели" : "моделей");
        info.at = nowMs();
        return info;
    }
    static std::set<std::string>& noTools() { static std::set<std::string> s; return s; }
    static std::string trimSlash(std::string u) { while (endsWith(u, "/")) u.pop_back(); return u; }
    static std::string nativeRoot(const Provider& p) { std::string u = trimSlash(p.baseURL); if (endsWith(u, "/v1")) u.resize(u.size() - 3); return u; }
    static bool toolsUnsupported(const std::string& e) {
        std::string l = lower(e);
        return l.find("does not support tools") != std::string::npos || l.find("tools is not supported") != std::string::npos ||
               l.find("tool use is not supported") != std::string::npos || l.find("does not support function") != std::string::npos ||
               l.find("tool_choice") != std::string::npos || l.find("tools are not supported") != std::string::npos ||
               l.find("function calling is not") != std::string::npos || l.find("--jinja") != std::string::npos ||
               (l.find("tool") != std::string::npos && l.find("not support") != std::string::npos);
    }
    static std::string curlBase(const Provider& p) {
        return std::string("curl -sS ") + (p.local() ? "--noproxy " + shellQuote("*") + " " : "");
    }
    // Simple GET. Returns HTTP status, 0 if there is no connection.
    static int get(const Provider& prov, const std::string& url, std::string& body, int timeoutSec, std::string* err = nullptr) {
        std::string key = Config::get().apiKey(prov);
        if (startsWith(lower(url), "http://")) {   // local / LAN servers: sockets, no curl process
            std::vector<std::string> h = {"User-Agent: NovaForge/" + std::string(VERSION)};
            if (!key.empty()) h.push_back("Authorization: Bearer " + key);
            return net::httpGet(url, h, body, prov.local() ? 400 : 3000, timeoutSec * 1000, err);
        }
        fs::path tmp = novaHome() / "tmp"; std::error_code ec; fs::create_directories(tmp, ec);
        fs::path hdrF = tmp / (makeId("get") + ".hdr"), outF = tmp / (makeId("get") + ".out");
        std::string hdr = "User-Agent: NovaForge/" + std::string(VERSION) + "\n";
        if (!key.empty()) hdr += "Authorization: Bearer " + key + "\n";
        if (prov.id == "anthropic") hdr += "x-api-key: " + key + "\nanthropic-version: 2023-06-01\n";
        writeFile(hdrF, hdr); restrictPerms(hdrF);
        auto r = runCommand(curlBase(prov) + "--connect-timeout " + std::to_string(std::min(timeoutSec, 5)) + " --max-time " + std::to_string(timeoutSec) +
                            " -o " + shellQuote(outF.string()) + " -w %{http_code} -H @" + shellQuote(hdrF.string()) + " " + shellQuote(url), 4000);
        body = readFile(outF);
        fs::remove(hdrF, ec); fs::remove(outF, ec);
        std::string o = trim(r.output);
        int code = 0; try { code = std::stoi(o.substr(o.size() >= 3 ? o.size() - 3 : 0)); } catch (...) {}
        if (code == 0 && err) *err = o.empty() ? "нет ответа" : truncateUtf8(firstLineOf(o), 120);
        return code;
    }

    static void writeRequest(const Json& body, const std::string& hdr, fs::path& bodyF, fs::path& hdrF) {
        fs::path tmp = novaHome() / "tmp"; std::error_code ec; fs::create_directories(tmp, ec);
        std::string id = makeId("req");
        bodyF = tmp / (id + ".json"); hdrF = tmp / (id + ".hdr");
        writeFile(bodyF, body.dump()); writeFile(hdrF, hdr); restrictPerms(hdrF);
    }
    static std::string errorFromBody(const std::string& raw0, int status, const std::string& url) {
        Json e; std::string msg = trim(raw0);
        if (Json::tryParse(msg, e)) {
            const Json& er = e.get("error");
            msg = er.isObject() ? er.getString("message", msg) : er.isString() ? er.str() : msg;
        }
        return "HTTP " + std::to_string(status) + ": " + (msg.empty() ? "нет ответа от " + url : truncateUtf8(msg, 600));
    }

    // OpenAI-compatible /chat/completions with SSE streaming.
    static LLMResponse openaiChat(const Provider& prov, const std::string& key, const std::string& mid, const LLMRequest& req, bool withTools, const DeltaFn& onDelta) {
        LLMResponse res;
        const std::string& pid = prov.id;
        Json body = Json::object();
        body["model"] = mid;
        body["messages"] = req.messages;
        body["stream"] = true;
        if (pid == "openai" || pid == "openrouter" || pid == "deepseek" || pid == "groq") {
            Json so = Json::object(); so["include_usage"] = true; body["stream_options"] = so;
        }
        if (withTools) { body["tools"] = req.tools; body["tool_choice"] = "auto"; }
        if (req.temperature >= 0) body["temperature"] = req.temperature;

        std::string hdr = "Content-Type: application/json\nAccept: text/event-stream\nUser-Agent: NovaForge/" + std::string(VERSION) + "\n";
        if (!key.empty()) hdr += "Authorization: Bearer " + key + "\n";
        if (pid == "anthropic") hdr += "x-api-key: " + key + "\nanthropic-version: 2023-06-01\n";
        if (pid == "openrouter") hdr += "HTTP-Referer: https://novaforge.dev\nX-Title: NovaForge\n";
        fs::path bodyF, hdrF; writeRequest(body, hdr, bodyF, hdrF);

        std::string url = trimSlash(prov.baseURL) + "/chat/completions";
        std::string cmd = curlBase(prov) + "-N --connect-timeout 10 --max-time 1800 -X POST " + shellQuote(url) + " -H @" + shellQuote(hdrF.string()) +
                          " --data-binary @" + shellQuote(bodyF.string()) + " -w " + shellQuote("\n__NOVA_STATUS__:%{http_code}\n");

        std::map<int, ToolCall> calls; std::string raw; int status = 0; bool sawData = false;
        ThinkFilter think;
        auto r = runCommand(cmd, 50 * 1024 * 1024, [&](const std::string& line0) {
            std::string line = trim(line0);
            if (startsWith(line, "__NOVA_STATUS__:")) { try { status = std::stoi(line.substr(16)); } catch (...) {} return; }
            if (!startsWith(line, "data:")) { raw += line0; return; }
            std::string payload = trim(line.substr(5));
            if (payload == "[DONE]") return;
            Json chunk; if (!Json::tryParse(payload, chunk)) return;
            sawData = true;
            if (chunk.has("error")) { const Json& e = chunk.get("error"); res.error = e.isString() ? e.str() : e.getString("message", payload); return; }
            const Json& usage = chunk.get("usage");
            if (usage.isObject()) { res.promptTokens = (long long)usage.getNum("prompt_tokens"); res.completionTokens = (long long)usage.getNum("completion_tokens"); }
            const Json& choices = chunk.get("choices");
            if (!choices.isArray() || choices.size() == 0) return;
            const Json& ch = choices[0];
            if (ch.get("finish_reason").isString()) res.finishReason = ch.get("finish_reason").str();
            const Json& delta = ch.get("delta");
            if (delta.get("content").isString()) {
                std::string d = think.feed(delta.get("content").str());
                res.content += d; if (onDelta && !d.empty()) onDelta(d);
            }
            for (auto& tc : delta.get("tool_calls").arr()) {
                // Some providers omit "index": a chunk with a new id starts a call, otherwise it continues the last one.
                int idx;
                if (tc.get("index").isNumber()) idx = (int)tc.getNum("index");
                else if (tc.get("id").isString() && !tc.get("id").str().empty()) idx = calls.empty() ? 0 : calls.rbegin()->first + 1;
                else idx = calls.empty() ? 0 : calls.rbegin()->first;
                ToolCall& c = calls[idx];
                if (tc.get("id").isString() && !tc.get("id").str().empty()) c.id = tc.get("id").str();
                const Json& fn = tc.get("function");
                if (fn.get("name").isString()) c.name += fn.get("name").str();
                if (fn.get("arguments").isString()) c.arguments += fn.get("arguments").str();
                else if (fn.get("arguments").isObject()) c.arguments = fn.get("arguments").dump();
            }
        });
        std::error_code ec; fs::remove(bodyF, ec); fs::remove(hdrF, ec);
        { std::string tail = think.flush(); res.content += tail; if (onDelta && !tail.empty()) onDelta(tail); }
        if (r.cancelled) { res.cancelled = true; res.error = "прервано"; return res; }

        for (auto& kv : calls) { if (kv.second.id.empty()) kv.second.id = makeId("call"); if (!kv.second.name.empty()) res.toolCalls.push_back(kv.second); }
        if (res.error.empty() && (status >= 400 || (!sawData && status != 200))) res.error = errorFromBody(raw, status, url);
        if (!res.completionTokens) res.completionTokens = (long long)(res.content.size() / 4);
        return res;
    }

    // Ollama native /api/chat (NDJSON streaming). Unlike /v1 it accepts options.num_ctx,
    // without which long prompts are silently cut to the default context.
    static LLMResponse ollamaChat(const Provider& prov, const std::string& mid, const LLMRequest& req, bool withTools, const DeltaFn& onDelta) {
        LLMResponse res;
        Json msgs = Json::array(); std::map<std::string, std::string> callNames;
        for (auto& m : req.messages.arr()) {
            std::string role = m.getString("role");
            Json o = Json::object(); o["role"] = role; o["content"] = m.getString("content");
            if (role == "assistant" && m.get("tool_calls").size()) {
                Json tcs = Json::array();
                for (auto& tc : m.get("tool_calls").arr()) {
                    const Json& fn = tc.get("function");
                    Json args; if (!Json::tryParse(fn.getString("arguments", "{}"), args) || !args.isObject()) args = Json::object();
                    callNames[tc.getString("id")] = fn.getString("name");
                    tcs.push(Json::object().set("function", Json::object().set("name", fn.getString("name")).set("arguments", args)));
                }
                o["tool_calls"] = tcs;
            }
            if (role == "tool") { auto it = callNames.find(m.getString("tool_call_id")); if (it != callNames.end()) o["tool_name"] = it->second; }
            msgs.push(o);
        }
        Json body = Json::object();
        body["model"] = mid; body["messages"] = msgs; body["stream"] = true;
        if (withTools) body["tools"] = req.tools;
        Json opts = Json::object();
        double ctx = Config::get().data.getNum("ollama_num_ctx", 8192);
        if (ctx >= 512) opts["num_ctx"] = ctx;
        if (req.temperature >= 0) opts["temperature"] = req.temperature;
        body["options"] = opts;

        std::string hdr = "Content-Type: application/json\nUser-Agent: NovaForge/" + std::string(VERSION) + "\n";
        fs::path bodyF, hdrF; writeRequest(body, hdr, bodyF, hdrF);
        std::string url = nativeRoot(prov) + "/api/chat";
        std::string cmd = curlBase(prov) + "-N --connect-timeout 10 --max-time 1800 -X POST " + shellQuote(url) + " -H @" + shellQuote(hdrF.string()) +
                          " --data-binary @" + shellQuote(bodyF.string()) + " -w " + shellQuote("\n__NOVA_STATUS__:%{http_code}\n");
        std::string raw; int status = 0; bool sawData = false; ThinkFilter think;
        auto r = runCommand(cmd, 50 * 1024 * 1024, [&](const std::string& line0) {
            std::string line = trim(line0);
            if (startsWith(line, "__NOVA_STATUS__:")) { try { status = std::stoi(line.substr(16)); } catch (...) {} return; }
            Json j; if (line.empty() || line[0] != '{' || !Json::tryParse(line, j)) { raw += line0; return; }
            if (j.get("error").isString()) { res.error = j.get("error").str(); return; }
            if (!j.has("message") && !j.has("done")) { raw += line0; return; }
            sawData = true;
            const Json& msg = j.get("message");
            if (msg.get("content").isString() && !msg.get("content").str().empty()) {
                std::string d = think.feed(msg.get("content").str());
                res.content += d; if (onDelta && !d.empty()) onDelta(d);
            }
            for (auto& tc : msg.get("tool_calls").arr()) {
                const Json& fn = tc.get("function");
                ToolCall c; c.id = makeId("call"); c.name = fn.getString("name");
                c.arguments = fn.get("arguments").isString() ? fn.get("arguments").str() : fn.get("arguments").isObject() ? fn.get("arguments").dump() : "{}";
                if (!c.name.empty()) res.toolCalls.push_back(c);
            }
            if (j.getBool("done")) {
                res.finishReason = j.getString("done_reason", "stop");
                res.promptTokens = (long long)j.getNum("prompt_eval_count");
                res.completionTokens = (long long)j.getNum("eval_count");
            }
        });
        std::error_code ec; fs::remove(bodyF, ec); fs::remove(hdrF, ec);
        { std::string tail = think.flush(); res.content += tail; if (onDelta && !tail.empty()) onDelta(tail); }
        if (r.cancelled) { res.cancelled = true; res.error = "прервано"; return res; }
        if (res.error.empty() && (status >= 400 || (!sawData && status != 200))) res.error = errorFromBody(raw, status, url);
        if (!res.completionTokens) res.completionTokens = (long long)(res.content.size() / 4);
        return res;
    }
};

} // namespace nova
