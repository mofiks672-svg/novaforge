// NovaForge — minimal JSON library (no dependencies)
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>
#include <cstdio>
#include <cmath>
#include <cstdint>

namespace nova {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array  = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t_(Type::Bool), b_(b) {}
    Json(int v) : t_(Type::Number), n_((double)v) {}
    Json(long v) : t_(Type::Number), n_((double)v) {}
    Json(long long v) : t_(Type::Number), n_((double)v) {}
    Json(unsigned v) : t_(Type::Number), n_((double)v) {}
    Json(unsigned long v) : t_(Type::Number), n_((double)v) {}
    Json(unsigned long long v) : t_(Type::Number), n_((double)v) {}
    Json(double v) : t_(Type::Number), n_(v) {}
    Json(const char* s) : t_(Type::String), s_(s ? s : "") {}
    Json(const std::string& s) : t_(Type::String), s_(s) {}
    Json(std::string&& s) : t_(Type::String), s_(std::move(s)) {}

    static Json array()  { Json j; j.t_ = Type::Array;  return j; }
    static Json object() { Json j; j.t_ = Type::Object; return j; }

    Type type() const { return t_; }
    bool isNull()   const { return t_ == Type::Null; }
    bool isBool()   const { return t_ == Type::Bool; }
    bool isNumber() const { return t_ == Type::Number; }
    bool isString() const { return t_ == Type::String; }
    bool isArray()  const { return t_ == Type::Array; }
    bool isObject() const { return t_ == Type::Object; }

    const std::string& str() const { return s_; }
    std::string& str() { return s_; }
    double num() const { return n_; }
    bool boolean() const { return b_; }
    Array& arr() { if (t_ == Type::Null) t_ = Type::Array; return a_; }
    const Array& arr() const { return a_; }
    Object& obj() { if (t_ == Type::Null) t_ = Type::Object; return o_; }
    const Object& obj() const { return o_; }

    size_t size() const { return t_ == Type::Array ? a_.size() : t_ == Type::Object ? o_.size() : 0; }

    bool has(const std::string& k) const {
        if (t_ != Type::Object) return false;
        for (auto& p : o_) if (p.first == k) return true;
        return false;
    }
    Json& operator[](const std::string& k) {
        if (t_ == Type::Null) t_ = Type::Object;
        for (auto& p : o_) if (p.first == k) return p.second;
        o_.emplace_back(k, Json());
        return o_.back().second;
    }
    Json& operator[](const char* k) { return (*this)[std::string(k)]; }
    const Json& get(const std::string& k) const {
        static const Json nil;
        if (t_ != Type::Object) return nil;
        for (auto& p : o_) if (p.first == k) return p.second;
        return nil;
    }
    Json& operator[](size_t i) { return a_.at(i); }
    const Json& operator[](size_t i) const { return a_.at(i); }

    Json& set(const std::string& k, Json v) { (*this)[k] = std::move(v); return *this; }
    Json& push(Json v) { if (t_ == Type::Null) t_ = Type::Array; a_.push_back(std::move(v)); return *this; }
    bool erase(const std::string& k) {
        for (auto it = o_.begin(); it != o_.end(); ++it) if (it->first == k) { o_.erase(it); return true; }
        return false;
    }

    std::string getString(const std::string& k, const std::string& def = "") const {
        const Json& v = get(k); return v.isString() ? v.s_ : def;
    }
    double getNum(const std::string& k, double def = 0) const {
        const Json& v = get(k);
        if (v.isNumber()) return v.n_;
        if (v.isString()) { try { size_t used = 0; double d = std::stod(v.s_, &used); if (used == v.s_.size()) return d; } catch (...) {} }
        return def;
    }
    bool getBool(const std::string& k, bool def = false) const {
        const Json& v = get(k);
        if (v.isBool()) return v.b_;
        if (v.isString()) { if (v.s_ == "true") return true; if (v.s_ == "false") return false; }
        return def;
    }

    // ---------- serialization ----------
    std::string dump(int indent = -1) const { std::string out; write(out, indent, 0); return out; }

    static void escape(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                    else out += (char)c;
            }
        }
        out += '"';
    }

    // ---------- parsing ----------
    static Json parse(const std::string& text) {
        size_t i = 0; Json j = parseValue(text, i); skipWs(text, i);
        if (i != text.size()) throw std::runtime_error("JSON: trailing characters at " + std::to_string(i));
        return j;
    }
    static bool tryParse(const std::string& text, Json& out) {
        try { out = parse(text); return true; } catch (...) { return false; }
    }

private:
    Type t_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    Array a_;
    Object o_;

    void write(std::string& out, int indent, int depth) const {
        auto nl = [&](int d) { if (indent >= 0) { out += '\n'; out.append((size_t)(indent * d), ' '); } };
        switch (t_) {
            case Type::Null: out += "null"; break;
            case Type::Bool: out += b_ ? "true" : "false"; break;
            case Type::Number: {
                char buf[64];
                if (std::isfinite(n_) && std::floor(n_) == n_ && std::fabs(n_) < 1e15) snprintf(buf, sizeof buf, "%lld", (long long)n_);
                else if (!std::isfinite(n_)) snprintf(buf, sizeof buf, "null");
                else snprintf(buf, sizeof buf, "%.15g", n_);
                out += buf; break;
            }
            case Type::String: escape(out, s_); break;
            case Type::Array:
                if (a_.empty()) { out += "[]"; break; }
                out += '[';
                for (size_t i = 0; i < a_.size(); ++i) { if (i) out += ','; nl(depth + 1); a_[i].write(out, indent, depth + 1); }
                nl(depth); out += ']'; break;
            case Type::Object:
                if (o_.empty()) { out += "{}"; break; }
                out += '{';
                for (size_t i = 0; i < o_.size(); ++i) {
                    if (i) out += ',';
                    nl(depth + 1);
                    escape(out, o_[i].first); out += indent >= 0 ? ": " : ":";
                    o_[i].second.write(out, indent, depth + 1);
                }
                nl(depth); out += '}'; break;
        }
    }

    static void skipWs(const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    static uint32_t hex4(const std::string& s, size_t i) {
        if (i + 4 > s.size()) throw std::runtime_error("JSON: bad \\u escape");
        return (uint32_t)std::stoul(s.substr(i, 4), nullptr, 16);
    }
    static std::string parseString(const std::string& s, size_t& i) {
        std::string out; ++i; // opening quote
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) break;
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break;
                    case 'r': out += '\r'; break; case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break; case '/': out += '/'; break;
                    case '\\': out += '\\'; break; case '"': out += '"'; break;
                    case 'u': {
                        uint32_t cp = hex4(s, i); i += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            uint32_t lo = hex4(s, i + 2);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                        }
                        appendUtf8(out, cp); break;
                    }
                    default: out += e;
                }
            } else out += c;
        }
        throw std::runtime_error("JSON: unterminated string");
    }
    static Json parseValue(const std::string& s, size_t& i) {
        skipWs(s, i);
        if (i >= s.size()) throw std::runtime_error("JSON: unexpected end");
        char c = s[i];
        if (c == '{') {
            Json j = object(); ++i; skipWs(s, i);
            if (i < s.size() && s[i] == '}') { ++i; return j; }
            while (true) {
                skipWs(s, i);
                if (i >= s.size() || s[i] != '"') throw std::runtime_error("JSON: expected key");
                std::string k = parseString(s, i); skipWs(s, i);
                if (i >= s.size() || s[i] != ':') throw std::runtime_error("JSON: expected ':'");
                ++i; j.o_.emplace_back(k, parseValue(s, i)); skipWs(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return j; }
                throw std::runtime_error("JSON: expected ',' or '}'");
            }
        }
        if (c == '[') {
            Json j = array(); ++i; skipWs(s, i);
            if (i < s.size() && s[i] == ']') { ++i; return j; }
            while (true) {
                j.a_.push_back(parseValue(s, i)); skipWs(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return j; }
                throw std::runtime_error("JSON: expected ',' or ']'");
            }
        }
        if (c == '"') return Json(parseString(s, i));
        if (s.compare(i, 4, "true") == 0) { i += 4; return Json(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return Json(false); }
        if (s.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        size_t st = i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
        if (st == i) throw std::runtime_error(std::string("JSON: unexpected char '") + c + "'");
        return Json(std::stod(s.substr(st, i - st)));
    }
};

} // namespace nova
