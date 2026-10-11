// NovaForge — configuration, providers, credentials
#pragma once
#include "json.hpp"
#include "util.hpp"
#include <algorithm>

namespace nova {

inline const char* VERSION = "1.5.0";

// Host part of an http(s) URL, lower-case, without port and brackets.
inline std::string urlHost(const std::string& url) {
    size_t p = url.find("://"); std::string h = p == std::string::npos ? url : url.substr(p + 3);
    h = h.substr(0, h.find_first_of("/?#"));
    if (auto at = h.rfind('@'); at != std::string::npos) h = h.substr(at + 1);
    if (!h.empty() && h[0] == '[') return lower(h.substr(1, h.find(']') - 1));
    return lower(h.substr(0, h.find(':')));
}
inline int urlPort(const std::string& url) {
    size_t p = url.find("://"); std::string h = p == std::string::npos ? url : url.substr(p + 3);
    h = h.substr(0, h.find_first_of("/?#"));
    size_t c = h.rfind(':'); if (c == std::string::npos || (h.find(']') > c && h[0] == '[')) return 0;
    try { return std::stoi(h.substr(c + 1)); } catch (...) { return 0; }
}
// Servers on this machine or in the local network (Ollama, LM Studio, llama.cpp, vLLM…).
inline bool isLocalHost(const std::string& h) {
    if (h == "localhost" || h == "::1" || h == "0.0.0.0" || h == "host.docker.internal") return true;
    if (startsWith(h, "127.") || startsWith(h, "10.") || startsWith(h, "192.168.") || endsWith(h, ".local")) return true;
    if (startsWith(h, "172.")) { int b = std::atoi(h.c_str() + 4); return b >= 16 && b <= 31; }
    return false;
}

struct Provider {
    std::string id, name, baseURL, envKey;
    std::vector<std::string> models;
    bool needsKey = true;
    bool local() const { return isLocalHost(urlHost(baseURL)); }
    // Ollama is talked to through its native API (/api/chat): it allows setting the context size.
    bool ollama() const { return id == "ollama" || (local() && urlPort(baseURL) == 11434); }
};

// Known APIs. No model lists here: models are loaded from the provider after it is connected
// (GET /models, for Ollama /api/tags) and cached in config.json.
inline std::vector<Provider> builtinProviders() {
    return {
        {"openai", "OpenAI", "https://api.openai.com/v1", "OPENAI_API_KEY", {}},
        {"anthropic", "Anthropic", "https://api.anthropic.com/v1", "ANTHROPIC_API_KEY", {}},
        {"google", "Google Gemini", "https://generativelanguage.googleapis.com/v1beta/openai", "GEMINI_API_KEY", {}},
        {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "OPENROUTER_API_KEY", {}},
        {"deepseek", "DeepSeek", "https://api.deepseek.com/v1", "DEEPSEEK_API_KEY", {}},
        {"groq", "Groq", "https://api.groq.com/openai/v1", "GROQ_API_KEY", {}},
        {"mistral", "Mistral", "https://api.mistral.ai/v1", "MISTRAL_API_KEY", {}},
        {"xai", "xAI", "https://api.x.ai/v1", "XAI_API_KEY", {}},
        {"together", "Together AI", "https://api.together.xyz/v1", "TOGETHER_API_KEY", {}},
        {"ollama", "Ollama", "http://127.0.0.1:11434/v1", "", {}, false},
        {"lmstudio", "LM Studio", "http://127.0.0.1:1234/v1", "", {}, false},
        {"llamacpp", "llama.cpp server", "http://127.0.0.1:8080/v1", "", {}, false},
    };
}

class Config {
public:
    Json data;
    fs::path globalPath, projectPath, authPath;

    static Config& get() { static Config c; return c; }

    void load() {
        globalPath = novaHome() / "config.json";
        projectPath = fs::current_path() / "novaforge.json";
        authPath = novaHome() / "auth.json";
        data = defaults();
        merge(data, readJson(globalPath));
        merge(data, readJson(projectPath));
        auth_ = readJson(authPath);
        if (!auth_.isObject()) auth_ = Json::object();
        std::string m = env("NOVA_MODEL"); if (!m.empty()) data["model"] = m;
    }

    static Json defaults() {
        Json d = Json::object();
        d["$schema"] = "https://novaforge.dev/config.json";
        d["model"] = "";              // nothing by default: chosen after an API is connected
        d["small_model"] = "";
        d["theme"] = "forge";
        d["intro"] = true;
        d["intro_mode"] = "always";   // always: forge animation on every start · 6h: at most every 6 hours
        d["animation_speed"] = 1.0;   // intro tempo: 1 = default, 0.7 = slower, 1.5 = faster, 0 = no animation delays
        d["project_instructions"] = true;   // new projects get a NOVA.md template
        d["projects_dir"] = "";
        d["default_project"] = "default project";
        d["ollama_num_ctx"] = 8192;
        d["max_steps"] = 40;
        Json perm = Json::object(); perm["edit"] = "ask"; perm["bash"] = "ask"; perm["webfetch"] = "allow";
        d["permission"] = perm;
        Json srv = Json::object(); srv["port"] = 4096; srv["host"] = "127.0.0.1"; srv["api_key"] = "";
        d["server"] = srv;
        d["providers"] = Json::object();
        d["instructions"] = Json::array();
        d["autoupdate_title"] = true;
        d["glyphs"] = "auto";         // auto | rich | safe (safe: only characters every console font has)
        return d;
    }

    static Json readJson(const fs::path& p) {
        std::string s; Json j;
        if (readFile(p, s) && Json::tryParse(s, j)) return j;
        return Json::object();
    }
    static void merge(Json& base, const Json& over) {
        if (!over.isObject()) return;
        for (auto& kv : over.obj()) {
            if (kv.second.isObject() && base.get(kv.first).isObject()) merge(base[kv.first], kv.second);
            else base[kv.first] = kv.second;
        }
    }

    void saveGlobal(const std::string& key, const Json& value) {
        Json g = readJson(globalPath); g[key] = value;
        writeFile(globalPath, g.dump(2)); data[key] = value;
    }

    fs::path projectsDir() const {
        std::string p = data.getString("projects_dir");
        if (!p.empty()) { if (p[0] == '~') p = homeDir().string() + p.substr(1); return fs::path(p); }
#ifdef _WIN32
        fs::path fromInstaller = installerSetting(L"ProjectsDir");   // folder chosen in the installer
        if (!fromInstaller.empty()) return fromInstaller;
#endif
        return documentsDir() / "NovaForge";
    }

    // Add or update an API connection (OpenAI-compatible endpoint) in the global config.
    void saveProvider(const std::string& id, const std::string& name, const std::string& baseURL,
                      const std::vector<std::string>& models, bool needsKey) {
        Json g = readJson(globalPath);
        Json& provs = g["providers"]; if (!provs.isObject()) provs = Json::object();
        Json p = provs.get(id).isObject() ? provs.get(id) : Json::object();
        if (!name.empty()) p["name"] = name;
        if (!baseURL.empty()) p["baseURL"] = baseURL;
        Json ms = Json::array(); for (auto& m : models) ms.push(m); p["models"] = ms;
        p["needs_key"] = needsKey;
        provs[id] = p;
        writeFile(globalPath, g.dump(2));
        data["providers"] = provs;
    }
    // Cache the model list loaded from the provider (replaces the previous list).
    void saveModels(const std::string& id, const std::vector<std::string>& models) {
        Json g = readJson(globalPath);
        Json& provs = g["providers"]; if (!provs.isObject()) provs = Json::object();
        Json p = provs.get(id).isObject() ? provs.get(id) : Json::object();
        Json ms = Json::array(); for (auto& m : models) ms.push(m);
        p["models"] = ms; p["models_updated"] = isoTime();
        provs[id] = p;
        writeFile(globalPath, g.dump(2));
        Json& dp = data["providers"]; if (!dp.isObject()) dp = Json::object();
        dp[id]["models"] = ms; dp[id]["models_updated"] = p["models_updated"];
    }
    bool removeProvider(const std::string& id) {
        Json g = readJson(globalPath);
        Json& provs = g["providers"];
        bool r = provs.isObject() && provs.erase(id);
        if (r) { writeFile(globalPath, g.dump(2)); data["providers"].erase(id); }
        removeKey(id);
        return r;
    }
    bool isBuiltinProvider(const std::string& id) const {
        for (auto& p : builtinProviders()) if (p.id == id) return true;
        return false;
    }

    std::string model() const { return data.getString("model"); }
    std::string permission(const std::string& tool) const { return data.get("permission").getString(tool, "ask"); }

    // ---------- providers ----------
    std::vector<Provider> providers() const {
        auto list = builtinProviders();
        const Json& custom = data.get("providers");
        for (auto& kv : custom.obj()) {
            bool found = false;
            for (auto& p : list) if (p.id == kv.first) {
                found = true;
                if (kv.second.has("baseURL")) p.baseURL = kv.second.getString("baseURL");
                if (kv.second.has("env")) p.envKey = kv.second.getString("env");
                for (auto& m : kv.second.get("models").arr())
                    if (m.isString() && std::find(p.models.begin(), p.models.end(), m.str()) == p.models.end()) p.models.push_back(m.str());
            }
            if (!found) {
                Provider p{kv.first, kv.second.getString("name", kv.first), kv.second.getString("baseURL"), kv.second.getString("env"), {}};
                for (auto& m : kv.second.get("models").arr()) if (m.isString()) p.models.push_back(m.str());
                p.needsKey = kv.second.getBool("needs_key", true);
                list.push_back(p);
            }
        }
        return list;
    }
    bool findProvider(const std::string& id, Provider& out) const {
        for (auto& p : providers()) if (p.id == id) { out = p; return true; }
        return false;
    }
    std::string apiKey(const Provider& p) const {
        std::string k = auth_.get(p.id).getString("key");
        if (!k.empty()) return k;
        if (!p.envKey.empty()) return env(p.envKey.c_str());
        return "";
    }
    void setKey(const std::string& provider, const std::string& key) {
        Json e = Json::object(); e["type"] = "api"; e["key"] = key; e["added"] = isoTime();
        auth_[provider] = e; writeFile(authPath, auth_.dump(2)); restrictPerms(authPath);
    }
    bool removeKey(const std::string& provider) {
        bool r = auth_.erase(provider);
        if (r) { writeFile(authPath, auth_.dump(2)); restrictPerms(authPath); }
        return r;
    }
    const Json& auth() const { return auth_; }

    static std::pair<std::string, std::string> splitModel(const std::string& m) {
        size_t p = m.find('/');
        if (p == std::string::npos) return {"", m};
        return {m.substr(0, p), m.substr(p + 1)};
    }

private:
    Json auth_;
};

} // namespace nova
