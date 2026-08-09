// =============================================================================
//  ext/json/json.hpp  --  minimal, dependency-free JSON value type for the
//  PolyFEA headless regression harness.
//
//  Kept dependency-free and offline-safe. This is NOT the
//  full nlohmann/json library; it is a compact hand-written implementation that
//  supports exactly what the scenario runner needs: parse a scenario file,
//  serialise a pretty-printed report, preserve object key insertion order (so
//  reports diff cleanly), and walk dotted paths for `asserts[].path`.
//
//  Namespace: minijson.  Single type: minijson::Value.
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace minijson {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Type type = Type::Null;
    bool        b   = false;
    double      num = 0.0;
    std::string str;
    std::vector<Value> arr;
    // Ordered to keep report output stable/diffable.
    std::vector<std::pair<std::string, Value>> obj;

    Value() = default;
    static Value makeObject() { Value v; v.type = Type::Object; return v; }
    static Value makeArray()  { Value v; v.type = Type::Array;  return v; }
    static Value makeNull()   { return Value(); }

    Value(bool x)               { type = Type::Bool;   b = x; }
    Value(double x)             { type = Type::Number; num = x; }
    Value(int x)                { type = Type::Number; num = static_cast<double>(x); }
    Value(long long x)          { type = Type::Number; num = static_cast<double>(x); }
    Value(const char* x)        { type = Type::String; str = x; }
    Value(const std::string& x) { type = Type::String; str = x; }

    bool isObject() const { return type == Type::Object; }
    bool isArray()  const { return type == Type::Array;  }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isBool()   const { return type == Type::Bool;   }
    bool isNull()   const { return type == Type::Null;   }

    // --- Object access ---------------------------------------------------
    bool contains(const std::string& key) const {
        for (const auto& kv : obj) if (kv.first == key) return true;
        return false;
    }
    const Value* find(const std::string& key) const {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    // Insert or overwrite (object). Preserves insertion order.
    void set(const std::string& key, Value v) {
        if (type != Type::Object) { type = Type::Object; }
        for (auto& kv : obj) if (kv.first == key) { kv.second = std::move(v); return; }
        obj.emplace_back(key, std::move(v));
    }
    void push(Value v) {
        if (type != Type::Array) { type = Type::Array; }
        arr.emplace_back(std::move(v));
    }

    // Typed getters with defaults.
    double asNumber(double def = 0.0) const { return isNumber() ? num : def; }
    bool   asBool(bool def = false)   const { return isBool()   ? b   : def; }
    std::string asString(const std::string& def = "") const { return isString() ? str : def; }

    // --- Serialisation ---------------------------------------------------
    std::string dump(int indent = 2) const {
        std::ostringstream os;
        dumpTo(os, indent, 0);
        return os.str();
    }

private:
    static void writeEscaped(std::ostream& os, const std::string& s) {
        os << '"';
        for (char c : s) {
            switch (c) {
                case '"':  os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\n': os << "\\n";  break;
                case '\r': os << "\\r";  break;
                case '\t': os << "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                        os << buf;
                    } else {
                        os << c;
                    }
            }
        }
        os << '"';
    }
    static void writeNumber(std::ostream& os, double n) {
        if (std::isnan(n) || std::isinf(n)) { os << "null"; return; }
        // Integers print without decimal point; otherwise use a high-precision
        // round-trippable representation.
        if (n == std::floor(n) && std::fabs(n) < 1e15) {
            os << static_cast<long long>(n);
        } else {
            std::ostringstream tmp;
            tmp << std::setprecision(17) << n;
            os << tmp.str();
        }
    }
    void dumpTo(std::ostream& os, int indent, int depth) const {
        const std::string pad(static_cast<size_t>(indent) * (depth + 1), ' ');
        const std::string padEnd(static_cast<size_t>(indent) * depth, ' ');
        switch (type) {
            case Type::Null:   os << "null"; break;
            case Type::Bool:   os << (b ? "true" : "false"); break;
            case Type::Number: writeNumber(os, num); break;
            case Type::String: writeEscaped(os, str); break;
            case Type::Array:
                if (arr.empty()) { os << "[]"; break; }
                os << "[\n";
                for (size_t i = 0; i < arr.size(); ++i) {
                    os << pad;
                    arr[i].dumpTo(os, indent, depth + 1);
                    if (i + 1 < arr.size()) os << ",";
                    os << "\n";
                }
                os << padEnd << "]";
                break;
            case Type::Object:
                if (obj.empty()) { os << "{}"; break; }
                os << "{\n";
                for (size_t i = 0; i < obj.size(); ++i) {
                    os << pad;
                    writeEscaped(os, obj[i].first);
                    os << ": ";
                    obj[i].second.dumpTo(os, indent, depth + 1);
                    if (i + 1 < obj.size()) os << ",";
                    os << "\n";
                }
                os << padEnd << "}";
                break;
        }
    }
};

// ---------------------------------------------------------------------------
// Parser: recursive descent. Throws std::runtime_error with a byte offset on
// malformed input (the harness turns this into an exit-2 parse message).
// ---------------------------------------------------------------------------
class Parser {
public:
    explicit Parser(const std::string& src) : s(src) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (pos != s.size())
            fail("trailing characters after top-level value");
        return v;
    }

private:
    const std::string& s;
    size_t pos = 0;

    [[noreturn]] void fail(const std::string& msg) const {
        std::ostringstream os;
        os << "JSON parse error at byte " << pos << ": " << msg;
        throw std::runtime_error(os.str());
    }

    void skipWs() {
        while (pos < s.size()) {
            char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos; continue; }
            // Line comments are not valid JSON but tolerate '//' to be friendly.
            if (c == '/' && pos + 1 < s.size() && s[pos + 1] == '/') {
                pos += 2;
                while (pos < s.size() && s[pos] != '\n') ++pos;
                continue;
            }
            break;
        }
    }

    char peek() const { return pos < s.size() ? s[pos] : '\0'; }

    Value parseValue() {
        skipWs();
        if (pos >= s.size()) fail("unexpected end of input");
        char c = s[pos];
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { Value v; v.type = Type::String; v.str = parseString(); return v; }
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
                fail(std::string("unexpected character '") + c + "'");
        }
    }

    Value parseObject() {
        Value v = Value::makeObject();
        ++pos; // {
        skipWs();
        if (peek() == '}') { ++pos; return v; }
        while (true) {
            skipWs();
            if (peek() != '"') fail("expected string key in object");
            std::string key = parseString();
            skipWs();
            if (peek() != ':') fail("expected ':' after object key");
            ++pos;
            Value val = parseValue();
            v.obj.emplace_back(std::move(key), std::move(val));
            skipWs();
            char c = peek();
            if (c == ',') { ++pos; continue; }
            if (c == '}') { ++pos; break; }
            fail("expected ',' or '}' in object");
        }
        return v;
    }

    Value parseArray() {
        Value v = Value::makeArray();
        ++pos; // [
        skipWs();
        if (peek() == ']') { ++pos; return v; }
        while (true) {
            Value val = parseValue();
            v.arr.emplace_back(std::move(val));
            skipWs();
            char c = peek();
            if (c == ',') { ++pos; continue; }
            if (c == ']') { ++pos; break; }
            fail("expected ',' or ']' in array");
        }
        return v;
    }

    std::string parseString() {
        ++pos; // opening quote
        std::string out;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos >= s.size()) fail("unterminated escape");
                char e = s[pos++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        if (pos + 4 > s.size()) fail("bad \\u escape");
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = s[pos++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp += static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp += static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp += static_cast<unsigned>(h - 'A' + 10);
                            else fail("bad hex digit in \\u escape");
                        }
                        // Minimal UTF-8 encode (BMP only; sufficient for scenarios).
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail("invalid escape character");
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
    }

    Value parseNumber() {
        size_t start = pos;
        if (peek() == '-') ++pos;
        while (pos < s.size()) {
            char c = s[pos];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') { ++pos; }
            else break;
        }
        std::string tok = s.substr(start, pos - start);
        try {
            Value v; v.type = Type::Number; v.num = std::stod(tok); return v;
        } catch (...) {
            fail("invalid number '" + tok + "'");
        }
    }

    Value parseBool() {
        if (s.compare(pos, 4, "true") == 0)  { pos += 4; Value v; v.type = Type::Bool; v.b = true;  return v; }
        if (s.compare(pos, 5, "false") == 0) { pos += 5; Value v; v.type = Type::Bool; v.b = false; return v; }
        fail("invalid literal");
    }

    Value parseNull() {
        if (s.compare(pos, 4, "null") == 0) { pos += 4; return Value::makeNull(); }
        fail("invalid literal");
    }
};

inline Value parse(const std::string& src) { return Parser(src).parse(); }

} // namespace minijson
