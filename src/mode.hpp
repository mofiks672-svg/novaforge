// NovaForge — work modes of the assistant: normal (full access) and plan (read-only).
#pragma once
#include "config.hpp"
#include <map>

namespace nova {

inline const std::vector<std::string>& allToolNames() {
    static const std::vector<std::string> t = {"read", "write", "edit", "multiedit", "list", "glob", "grep", "bash", "webfetch", "todowrite", "todoread"};
    return t;
}

// What the assistant may do in the current turn. Not user-configurable: there is one
// assistant, the mode only narrows its tools (Shift+Tab in the chat, "mode" in workflows/API).
struct Mode {
    std::string name = "normal";            // normal | plan
    std::string prompt;
    std::vector<std::string> tools;          // empty = all
    std::map<std::string, std::string> permission;   // permission group -> allow|ask|deny

    bool hasTool(const std::string& t) const { return tools.empty() || std::find(tools.begin(), tools.end(), t) != tools.end(); }
    bool readOnly() const { return name == "plan"; }

    static Mode normal() {
        Mode m;
        m.prompt = "Выполняй задачу полностью: изучи код, внеси правки инструментами edit/write, "
                   "проверь результат (сборка, тесты, линтер). Для многошаговой работы веди список задач через todowrite.";
        return m;
    }
    static Mode plan() {
        Mode m; m.name = "plan";
        m.tools = {"read", "list", "glob", "grep", "webfetch", "todowrite", "todoread", "bash"};
        m.permission = {{"edit", "deny"}, {"bash", "ask"}};
        m.prompt = "Режим plan: ты НЕ изменяешь файлы. Изучи кодовую базу и предложи детальный пошаговый план: "
                   "какие файлы затронуть, риски, тесты. Заканчивай ответ разделом «## План».";
        return m;
    }
    // "plan" → read-only, anything else (including names of removed agents in old configs) → normal.
    static Mode byName(const std::string& n) { return n == "plan" ? plan() : normal(); }
};

} // namespace nova
