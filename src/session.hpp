// NovaForge — sessions: persistence, undo/redo snapshots, export, usage
#pragma once
#include "config.hpp"
#include <map>
#include <mutex>
#include <set>

namespace nova {

struct FileSnapshot { std::string path; bool existed; std::string content; };

struct Session {
    std::string id, title, model, cwd;
    long long created = 0, updated = 0;
    Json messages = Json::array();   // OpenAI chat format (without system prompt)
    Json todos = Json::array();
    long long tokensIn = 0, tokensOut = 0;
    std::string parentId;            // set only in sessions of old versions (sub-agents); such sessions are hidden

    // undo/redo: each turn stores file snapshots taken before modification
    // plus the index of the first message of that turn.
    struct Turn { std::vector<FileSnapshot> files; size_t msgStart = 0; };
    struct Undone { std::vector<FileSnapshot> files; Json messages; };
    std::vector<Turn> undoStack;
    std::vector<Undone> redoStack;
    std::vector<FileSnapshot> currentTurn;
    size_t turnMsgStart = 0;

    static fs::path dir() { fs::path d = novaHome() / "sessions"; std::error_code ec; fs::create_directories(d, ec); return d; }

    static Session create(const std::string& model) {
        Session s; s.id = makeId("ses"); s.model = model; s.title = "Новая сессия";
        s.created = s.updated = nowMs(); s.cwd = fs::current_path().string(); return s;
    }

    Json toJson() const {
        Json j = Json::object();
        j["id"] = id; j["title"] = title; j["model"] = model; j["cwd"] = cwd;
        j["created"] = created; j["updated"] = updated; j["messages"] = messages; j["todos"] = todos;
        j["tokens_in"] = tokensIn; j["tokens_out"] = tokensOut;
        if (!parentId.empty()) j["parent"] = parentId;
        return j;
    }
    Json summaryJson() const {
        Json j = Json::object();
        j["id"] = id; j["title"] = title; j["model"] = model; j["cwd"] = cwd;
        j["created"] = created; j["updated"] = updated; j["messages"] = (double)messageCount();
        j["tokens_in"] = tokensIn; j["tokens_out"] = tokensOut; return j;
    }
    static Session fromJson(const Json& j) {
        Session s; s.id = j.getString("id"); s.title = j.getString("title");
        s.model = j.getString("model"); s.cwd = j.getString("cwd");
        s.created = (long long)j.getNum("created"); s.updated = (long long)j.getNum("updated");
        s.messages = j.get("messages").isArray() ? j.get("messages") : Json::array();
        s.todos = j.get("todos").isArray() ? j.get("todos") : Json::array();
        s.tokensIn = (long long)j.getNum("tokens_in"); s.tokensOut = (long long)j.getNum("tokens_out");
        s.parentId = j.getString("parent");
        return s;
    }

    void save() { updated = nowMs(); writeFile(dir() / (id + ".json"), toJson().dump()); }

    static bool validId(const std::string& id) { return safeName(id); }
    static bool load(const std::string& id, Session& out) {
        if (!validId(id)) return false;
        std::string s; Json j;
        if (!readFile(dir() / (id + ".json"), s) || !Json::tryParse(s, j)) return false;
        out = fromJson(j); return true;
    }
    static bool remove(const std::string& id) { if (!validId(id)) return false; std::error_code ec; return fs::remove(dir() / (id + ".json"), ec); }

    size_t messageCount() const { return summaryCount_ ? summaryCount_ : messages.size(); }

    // Lightweight list for menus: titles, dates, folders — without message history.
    // Parsed summaries are cached by file size and mtime, so repeated calls only stat the files.
    // Use Session::load(id) to get the full session.
    static std::vector<Session> list(size_t limit = 50) {
        struct Entry { uintmax_t size = 0; fs::file_time_type mtime{}; Session s; bool hidden = false; };
        static std::mutex mu; static std::map<std::string, Entry> cache;
        std::lock_guard<std::mutex> g(mu);
        std::vector<Session> v; std::set<std::string> seen; std::error_code ec;
        for (auto& e : fs::directory_iterator(dir(), ec)) {
            if (e.path().extension() != ".json") continue;
            std::string key = e.path().string(); seen.insert(key);
            uintmax_t size = e.file_size(ec); auto mtime = e.last_write_time(ec);
            auto it = cache.find(key);
            if (it == cache.end() || it->second.size != size || it->second.mtime != mtime) {
                Entry en; en.size = size; en.mtime = mtime;
                Json j; if (!Json::tryParse(readFile(e.path()), j)) continue;
                en.s = fromJson(j); en.hidden = !en.s.parentId.empty();
                en.s.summaryCount_ = en.s.messages.size();
                en.s.messages = Json::array(); en.s.todos = Json::array();
                it = cache.insert_or_assign(key, std::move(en)).first;
            }
            if (!it->second.hidden) v.push_back(it->second.s);
        }
        for (auto it = cache.begin(); it != cache.end();) it = seen.count(it->first) ? std::next(it) : cache.erase(it);
        std::sort(v.begin(), v.end(), [](const Session& a, const Session& b) { return a.updated > b.updated; });
        if (v.size() > limit) v.resize(limit);
        return v;
    }
    static size_t count() { return list(100000).size(); }

    // ---------- snapshots ----------
    void beginTurn() { currentTurn.clear(); turnMsgStart = messages.size(); }
    void snapshot(const fs::path& p) {
        std::string key = fs::absolute(p).lexically_normal().string();
        for (auto& s : currentTurn) if (s.path == key) return;
        std::error_code ec;
        FileSnapshot fsn{key, fs::is_regular_file(p, ec), ""};
        if (fsn.existed) readFile(p, fsn.content);
        currentTurn.push_back(fsn);
    }
    void endTurn() {
        undoStack.push_back({currentTurn, turnMsgStart});
        redoStack.clear();          // a new turn invalidates everything that was undone
        currentTurn.clear();
    }
    void clearHistory() { undoStack.clear(); redoStack.clear(); currentTurn.clear(); }

    static std::vector<FileSnapshot> applySnapshots(const std::vector<FileSnapshot>& snaps) {
        std::vector<FileSnapshot> inverse;
        for (auto it = snaps.rbegin(); it != snaps.rend(); ++it) {
            const auto& s = *it;
            std::error_code ec;
            FileSnapshot cur{s.path, fs::exists(s.path, ec), ""};
            if (cur.existed) readFile(s.path, cur.content);
            inverse.push_back(cur);
            if (s.existed) writeFile(s.path, s.content); else fs::remove(s.path, ec);
        }
        return inverse;
    }
    // Returns number of files restored, -1 if there is nothing to undo.
    int undo() {
        if (undoStack.empty()) return -1;
        Turn t = undoStack.back(); undoStack.pop_back();
        Undone u; u.files = applySnapshots(t.files); u.messages = Json::array();
        Json kept = Json::array();
        for (size_t i = 0; i < messages.size(); ++i) (i < t.msgStart ? kept : u.messages).push(messages[i]);
        messages = kept;
        int n = (int)t.files.size();
        redoStack.push_back(std::move(u));
        return n;
    }
    int redo() {
        if (redoStack.empty()) return -1;
        Undone u = redoStack.back(); redoStack.pop_back();
        Turn t; t.msgStart = messages.size(); t.files = applySnapshots(u.files);
        for (auto& m : u.messages.arr()) messages.push(m);
        int n = (int)u.files.size();
        undoStack.push_back(std::move(t));
        return n;
    }

    std::string exportMarkdown() const {
        std::ostringstream o;
        o << "# " << title << "\n\n";
        o << "> NovaForge " << VERSION << " · модель `" << (model.empty() ? "не выбрана" : model) << "` · " << isoTime(created) << "\n\n";
        for (auto& m : messages.arr()) {
            std::string role = m.getString("role");
            if (role == "user") o << "## Пользователь\n\n" << m.getString("content") << "\n\n";
            else if (role == "assistant") {
                if (!m.getString("content").empty()) o << "## Ассистент\n\n" << m.getString("content") << "\n\n";
                for (auto& tc : m.get("tool_calls").arr())
                    o << "Инструмент `" << tc.get("function").getString("name") << "`:\n\n```json\n"
                      << tc.get("function").getString("arguments") << "\n```\n\n";
            } else if (role == "tool") {
                o << "```text\n" << truncateUtf8(m.getString("content"), 1500) << "\n```\n\n";
            }
        }
        o << "---\nТокены: вход " << tokensIn << ", выход " << tokensOut << "\n";
        return o.str();
    }

private:
    size_t summaryCount_ = 0;        // number of messages when only a summary is loaded (list())
};

} // namespace nova
