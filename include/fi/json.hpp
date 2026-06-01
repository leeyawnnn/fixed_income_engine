#pragma once

// A small, self-contained JSON parser — just enough to read the demo's
// portfolio file without pulling in a third-party dependency. Supports
// null / bool / number / string / array / object with the common string
// escapes. Throws std::runtime_error on malformed input.

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fi::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool bval = false;
    double nval = 0.0;
    std::string sval;
    std::vector<Value> items;                              // Array
    std::vector<std::pair<std::string, Value>> members;   // Object

    bool is_null() const { return type == Type::Null; }
    double number() const {
        if (type != Type::Number) throw std::runtime_error("json: not a number");
        return nval;
    }
    const std::string& as_string() const {
        if (type != Type::String) throw std::runtime_error("json: not a string");
        return sval;
    }
    bool as_bool() const {
        if (type != Type::Bool) throw std::runtime_error("json: not a bool");
        return bval;
    }
    const std::vector<Value>& as_array() const {
        if (type != Type::Array) throw std::runtime_error("json: not an array");
        return items;
    }
    bool contains(const std::string& k) const {
        if (type != Type::Object) return false;
        for (const auto& m : members)
            if (m.first == k) return true;
        return false;
    }
    const Value& operator[](const std::string& k) const {
        for (const auto& m : members)
            if (m.first == k) return m.second;
        throw std::runtime_error("json: missing key '" + k + "'");
    }
};

namespace detail {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Value parse() {
        skip_ws();
        Value v = value();
        skip_ws();
        if (i_ != s_.size()) err("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;

    [[noreturn]] void err(const std::string& m) {
        throw std::runtime_error("json: " + m);
    }
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    void skip_ws() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    Value value() {
        skip_ws();
        switch (peek()) {
            case '{': return object();
            case '[': return array();
            case '"': {
                Value v;
                v.type = Value::Type::String;
                v.sval = string();
                return v;
            }
            case 't':
            case 'f': return boolean();
            case 'n': return null();
            default: return number();
        }
    }

    Value object() {
        Value v;
        v.type = Value::Type::Object;
        ++i_;  // '{'
        skip_ws();
        if (peek() == '}') { ++i_; return v; }
        while (true) {
            skip_ws();
            if (peek() != '"') err("expected string key");
            std::string key = string();
            skip_ws();
            if (peek() != ':') err("expected ':'");
            ++i_;
            v.members.emplace_back(std::move(key), value());
            skip_ws();
            char c = peek();
            ++i_;
            if (c == '}') break;
            if (c != ',') err("expected ',' or '}'");
        }
        return v;
    }

    Value array() {
        Value v;
        v.type = Value::Type::Array;
        ++i_;  // '['
        skip_ws();
        if (peek() == ']') { ++i_; return v; }
        while (true) {
            v.items.push_back(value());
            skip_ws();
            char c = peek();
            ++i_;
            if (c == ']') break;
            if (c != ',') err("expected ',' or ']'");
        }
        return v;
    }

    std::string string() {
        ++i_;  // opening quote
        std::string out;
        while (true) {
            if (i_ >= s_.size()) err("unterminated string");
            char c = s_[i_++];
            if (c == '"') break;
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) err("bad escape");
            char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i_ + 4 > s_.size()) err("bad \\u escape");
                    int code = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s_[i_++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= h - '0';
                        else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
                        else err("bad hex digit");
                    }
                    out += (code < 128) ? static_cast<char>(code) : '?';
                    break;
                }
                default: err("bad escape character");
            }
        }
        return out;
    }

    Value boolean() {
        Value v;
        v.type = Value::Type::Bool;
        if (s_.compare(i_, 4, "true") == 0) { v.bval = true; i_ += 4; }
        else if (s_.compare(i_, 5, "false") == 0) { v.bval = false; i_ += 5; }
        else err("invalid literal");
        return v;
    }

    Value null() {
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return Value{}; }
        err("invalid literal");
    }

    Value number() {
        const char* start = s_.c_str() + i_;
        char* end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start) err("invalid number");
        i_ += static_cast<std::size_t>(end - start);
        Value v;
        v.type = Value::Type::Number;
        v.nval = d;
        return v;
    }
};

}  // namespace detail

inline Value parse(const std::string& text) {
    return detail::Parser(text).parse();
}

}  // namespace fi::json
