#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace tickerbar {
// Minimal JSON reader for the Coinbase / CoinGecko API payloads. Std-only so
// the Linux test suite grades the EXACT parser the app ships (the app
// never re-parses with a second engine — one parser, one behaviour).
// Design rules, all deliberate:
//  - No exceptions cross the API: failures come back as std::nullopt.
//  - Depth-capped (64) and size-capped by the caller: a hostile or
//    corrupt body can waste neither stack nor time.
//  - Numbers land in double. Coinbase sends most numerics as STRINGS
//    ("last":"79736.11"); the parsers accept string-or-number for those
//    fields, converting here via asNumber().
//  - Strings keep their UTF-8 bytes; \u escapes decode to UTF-8
//    (surrogate pairs combined). Asset names are ASCII in practice.
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    bool isNull() const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isBool() const { return type == Type::Bool; }

    const Json* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
    const Json* at(size_t i) const {
        if (type != Type::Array || i >= array.size()) return nullptr;
        return &array[i];
    }
    // Numeric view of a field that may be a JSON number OR a numeric
    // string (Coinbase's habit). Anything else — null, object, a
    // non-numeric string — is nullopt: callers must not guess.
    std::optional<double> asNumber() const {
        if (type == Type::Number) return number;
        if (type == Type::String) {
            if (str.empty()) return std::nullopt;
            try {
                size_t used = 0;
                double v = std::stod(str, &used);
                if (used != str.size()) return std::nullopt; // trailing junk
                return v;
            } catch (...) { return std::nullopt; }
        }
        return std::nullopt;
    }
    std::optional<std::string> asString() const {
        if (type == Type::String) return str;
        return std::nullopt;
    }
    std::optional<bool> asBool() const {
        if (type == Type::Bool) return boolean;
        return std::nullopt;
    }
};

namespace jsondetail {
struct Parser {
    const char* p = nullptr;
    const char* end = nullptr;
    int depth = 0;
    static constexpr int kMaxDepth = 64;
    bool fail = false;

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool consume(char c) {
        skipWs();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }
    bool literal(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if ((size_t)(end - p) >= n && std::string(p, p + n) == lit) { p += n; return true; }
        return false;
    }
    Json parseValue() {
        Json v;
        if (fail) return v;
        skipWs();
        if (p >= end) { fail = true; return v; }
        if (++depth > kMaxDepth) { fail = true; return v; }
        char c = *p;
        if (c == '{') v = parseObject();
        else if (c == '[') v = parseArray();
        else if (c == '"') { auto s = parseString(); if (fail) return Json{}; v.type = Json::Type::String; v.str = std::move(s); }
        else if (c == 't') { if (!literal("true")) { fail = true; return v; } v.type = Json::Type::Bool; v.boolean = true; }
        else if (c == 'f') { if (!literal("false")) { fail = true; return v; } v.type = Json::Type::Bool; v.boolean = false; }
        else if (c == 'n') { if (!literal("null")) { fail = true; return v; } v.type = Json::Type::Null; }
        else if (c == '-' || (c >= '0' && c <= '9')) v = parseNumber();
        else fail = true;
        --depth;
        return v;
    }
    Json parseObject() {
        Json v; v.type = Json::Type::Object;
        ++p; // {
        if (consume('}')) return v;
        while (true) {
            skipWs();
            if (p >= end || *p != '"') { fail = true; return v; }
            auto key = parseString();
            if (fail) return v;
            if (!consume(':')) { fail = true; return v; }
            Json val = parseValue();
            if (fail) return v;
            v.object[std::move(key)] = std::move(val);
            if (consume(',')) continue;
            if (consume('}')) return v;
            fail = true; return v;
        }
    }
    Json parseArray() {
        Json v; v.type = Json::Type::Array;
        ++p; // [
        if (consume(']')) return v;
        while (true) {
            Json val = parseValue();
            if (fail) return v;
            v.array.push_back(std::move(val));
            if (consume(',')) continue;
            if (consume(']')) return v;
            fail = true; return v;
        }
    }
    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(uint32_t& out) {
        if (end - p < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i]; v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        p += 4; out = v; return true;
    }
    std::string parseString() {
        std::string out;
        ++p; // opening quote
        while (p < end) {
            char c = *p++;
            if (c == '"') return out;
            if (c == '\\') {
                if (p >= end) break;
                char e = *p++;
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!hex4(cp)) { fail = true; return out; }
                        if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate: expect \uXXXX low
                            if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                                p += 2; uint32_t lo = 0;
                                if (!hex4(lo)) { fail = true; return out; }
                                if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                else { fail = true; return out; }
                            } else { fail = true; return out; }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail = true; return out;
                }
            } else if ((unsigned char)c < 0x20) { fail = true; return out; }
            else out += c;
        }
        fail = true; return out; // unterminated
    }
    Json parseNumber() {
        Json v;
        const char* start = p;
        if (p < end && *p == '-') ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p;
        if (p == start) { fail = true; return v; }
        try {
            size_t used = 0;
            std::string s(start, p);
            v.number = std::stod(s, &used);
            if (used != s.size()) { fail = true; return v; }
        } catch (...) { fail = true; return v; }
        v.type = Json::Type::Number;
        return v;
    }
};
} // namespace jsondetail

// Parse a complete JSON document. Trailing non-whitespace is an error.
inline std::optional<Json> parseJson(const std::string& text) {
    if (text.empty() || text.size() > 16u * 1024u * 1024u) return std::nullopt;
    jsondetail::Parser ps{ text.data(), text.data() + text.size(), 0, false };
    Json v = ps.parseValue();
    if (ps.fail) return std::nullopt;
    ps.skipWs();
    if (ps.p != ps.end) return std::nullopt;
    return v;
}
} // namespace tickerbar
