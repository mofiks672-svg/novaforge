// NovaForge — engine: system prompt, tool-calling loop, compaction
#pragma once
#include "llm.hpp"
#include "tools.hpp"

namespace nova {

struct EngineEvents {
    std::function<void(const std::string&)> onText;
    std::function<void(int step)> onStep;
    std::function<void(const std::string&, const Json&)> onToolStart;
    std::function<void(const std::string&, const ToolResult&)> onToolEnd;
    std::function<bool(const std::string&, const std::string&, const std::string&)> askPermission;
    std::function<void(const std::string&)> onOutput;
    std::function<void()> onResponseEnd;
    std::function<void(const std::string&)> onNotice;
};

struct RunResult {
    bool ok = true, cancelled = false; std::string text, error; int steps = 0;
    Json toolLog = Json::array();
    long long tokensIn = 0, tokensOut = 0;
};

class Engine {
public:
    // Model of the session, otherwise the one chosen in settings. Empty when nothing is connected yet.
    static std::string modelFor(const Session& s) {
        if (!s.model.empty()) return s.model;
        return Config::get().model();
    }

    static std::string projectInstructions() {
        std::string out;
        for (const char* f : {"NOVA.md", "AGENTS.md", "CLAUDE.md"}) {
            fs::path p = fs::current_path() / f;
            if (fs::exists(p)) { out += "\n\n# Инструкции проекта (" + std::string(f) + ")\n" + truncateUtf8(readFile(p), 12000); break; }
        }
        fs::path g = novaHome() / "NOVA.md"; if (!fs::exists(g)) g = novaHome() / "AGENTS.md";
        if (fs::exists(g)) out += "\n\n# Глобальные инструкции пользователя\n" + truncateUtf8(readFile(g), 6000);
        for (auto& i : Config::get().data.get("instructions").arr())
            if (i.isString() && fs::exists(i.str())) out += "\n\n# " + i.str() + "\n" + truncateUtf8(readFile(i.str()), 6000);
        return out;
    }

    static std::string systemPrompt(const Mode& a) {
        std::ostringstream o;
        o << "Ты — NovaForge, AI-ассистент для разработки ПО и автоматизации, работающий в терминале пользователя.\n"
          << "Отвечай на языке пользователя, кратко и по делу. Используй Markdown.\n\n"
          << "Правила работы:\n"
          << "- Перед изменением файла прочитай его (read). Для правок предпочитай edit/multiedit, а не write.\n"
          << "- Используй glob/grep/list для навигации, не угадывай пути.\n"
          << "- Для многошаговых задач веди список через todowrite и обновляй статусы.\n"
          << "- После правок проверяй результат (сборка/тесты) через bash, если это уместно.\n"
          << "- Не выдумывай результаты команд. Сообщай о фактически сделанном.\n\n";
        o << a.prompt << "\n\n";
        o << "# Окружение\n"
          << "Рабочая директория: " << fs::current_path().string() << "\n"
          << "ОС: " << osName() << "\n"
          << "Дата: " << isoTime().substr(0, 10) << "\n"
          << "Git-репозиторий: " << (fs::exists(fs::current_path() / ".git") ? "да" : "нет") << "\n";
        o << projectInstructions();
        return o.str();
    }

    static RunResult run(Session& s, const Mode& mode, const std::string& userText, EngineEvents& ev) {
        RunResult rr;
        if (!userText.empty()) s.messages.push(Json::object().set("role", "user").set("content", userText));
        if (s.title == "Новая сессия" && !userText.empty()) s.title = truncateUtf8(trim(split(userText, '\n')[0]), 60);

        ToolContext ctx;
        ctx.session = &s; ctx.mode = &mode; ctx.askPermission = ev.askPermission; ctx.onOutput = ev.onOutput;

        std::string model = modelFor(s);
        if (model.empty()) {
            if (!userText.empty() && s.messages.size()) { Json kept = Json::array(); for (size_t i = 0; i + 1 < s.messages.size(); ++i) kept.push(s.messages[i]); s.messages = kept; }
            rr.ok = false; rr.error = "Модель не выбрана. Подключите API (/connect) или выберите модель (/model)."; return rr;
        }
        Json tools = ToolRegistry::get().schemasFor(mode);
        int maxSteps = (int)Config::get().data.getNum("max_steps", 40);
        std::string system = systemPrompt(mode);   // built once per turn: it reads instruction files from disk

        for (int step = 1; step <= maxSteps; ++step) {
            rr.steps = step;
            if (ev.onStep) ev.onStep(step);
            LLMRequest req; req.model = model; req.tools = tools; req.onNotice = ev.onNotice;
            req.messages.push(Json::object().set("role", "system").set("content", system));
            for (auto& m : s.messages.arr()) req.messages.push(m);

            LLMResponse res = LLM::chat(req, ev.onText);
            if (ev.onResponseEnd) ev.onResponseEnd();
            if (!res.notice.empty() && ev.onNotice) ev.onNotice(res.notice);
            rr.tokensIn += res.promptTokens; rr.tokensOut += res.completionTokens;
            s.tokensIn += res.promptTokens; s.tokensOut += res.completionTokens;
            if (res.cancelled || cancelFlag()) {
                // keep what was already said so the conversation stays consistent
                if (!trim(res.content).empty()) s.messages.push(Json::object().set("role", "assistant").set("content", res.content + "\n\n[ответ прерван пользователем]"));
                rr.ok = false; rr.cancelled = true; rr.error = "прервано"; return rr;
            }
            if (!res.ok()) {
                // nothing was answered: drop the user message of a failed first step so a retry is clean
                if (step == 1 && !userText.empty() && s.messages.size() && s.messages[s.messages.size() - 1].getString("role") == "user") {
                    Json kept = Json::array(); for (size_t i = 0; i + 1 < s.messages.size(); ++i) kept.push(s.messages[i]); s.messages = kept;
                }
                rr.ok = false; rr.error = res.error; return rr;
            }

            Json am = Json::object(); am["role"] = "assistant"; am["content"] = res.content;
            if (!res.toolCalls.empty()) {
                Json tcs = Json::array();
                for (auto& tc : res.toolCalls) {
                    Json fn = Json::object(); fn["name"] = tc.name; fn["arguments"] = tc.arguments.empty() ? "{}" : tc.arguments;
                    tcs.push(Json::object().set("id", tc.id).set("type", "function").set("function", fn));
                }
                am["tool_calls"] = tcs;
            }
            s.messages.push(am);
            if (!res.content.empty()) rr.text = res.content;
            if (res.toolCalls.empty()) return rr;

            for (auto& tc : res.toolCalls) {
                if (cancelFlag()) {   // every tool call needs a result, otherwise the API rejects the history
                    s.messages.push(Json::object().set("role", "tool").set("tool_call_id", tc.id).set("content", "[не выполнено: прервано пользователем]"));
                    continue;
                }
                Json args; Json::tryParse(tc.arguments.empty() ? "{}" : tc.arguments, args);
                if (ev.onToolStart) ev.onToolStart(tc.name, args);
                ToolResult tr = ToolRegistry::get().execute(tc.name, tc.arguments, ctx);
                if (ev.onToolEnd) ev.onToolEnd(tc.name, tr);
                rr.toolLog.push(Json::object().set("tool", tc.name).set("ok", tr.ok).set("title", tr.title));
                std::string out = tr.output.size() > 40000 ? tr.output.substr(0, 40000) + "\n…[обрезано]" : tr.output;
                s.messages.push(Json::object().set("role", "tool").set("tool_call_id", tc.id).set("content", out));
            }
            if (cancelFlag()) { rr.ok = false; rr.cancelled = true; rr.error = "прервано"; return rr; }
        }
        rr.text += "\n\n(достигнут лимит шагов: " + std::to_string(maxSteps) + ")";
        return rr;
    }

    // Summarize the conversation to free up context window
    static bool compact(Session& s, std::string& err) {
        if (s.messages.size() < 2) { err = "Нечего сжимать"; return false; }
        std::ostringstream o;
        for (auto& m : s.messages.arr()) {
            std::string role = m.getString("role");
            if (role == "tool") o << "[tool result] " << truncateUtf8(m.getString("content"), 800) << "\n";
            else {
                o << "[" << role << "] " << m.getString("content") << "\n";
                for (auto& tc : m.get("tool_calls").arr()) o << "  -> " << tc.get("function").getString("name") << " " << truncateUtf8(tc.get("function").getString("arguments"), 300) << "\n";
            }
        }
        std::string small = Config::get().data.getString("small_model"); if (small.empty()) small = modelFor(s);
        if (small.empty()) { err = "Модель не выбрана — /connect"; return false; }
        auto r = LLM::complete(small, "Ты сжимаешь историю диалога разработчика с AI-ассистентом. Сохрани: цель, принятые решения, изменённые файлы, текущий статус и следующие шаги. Пиши компактно, Markdown.", truncateUtf8(o.str(), 60000));
        if (!r.ok()) { err = r.error; return false; }
        s.messages = Json::array();
        s.messages.push(Json::object().set("role", "user").set("content", "Краткое содержание предыдущей части сессии:\n\n" + r.content));
        s.messages.push(Json::object().set("role", "assistant").set("content", "Понял контекст. Продолжаем."));
        s.clearHistory();
        return true;
    }
};

} // namespace nova
