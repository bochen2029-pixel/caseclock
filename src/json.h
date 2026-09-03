// caseclock · json.h — a small strict JSON value, parser and canonical writer.
//
// The canonical form is the reference tape's: Python json.dumps(obj, sort_keys=True,
// separators=(",", ":"), ensure_ascii=False). Keys sort by code point (UTF-8 byte order is the
// same order); strings escape only " \ and the C0 controls (\b \f \n \r \t named, the rest \u00xx,
// lowercase); everything else, including every non-ASCII code point and 0x7f, passes through raw.
// Integers are int64 and print in decimal; a non-integer number keeps its source text and is never
// produced by this tool.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace caseclock {

struct Json {
    enum Type { Null, Bool, Int, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    int64_t i = 0;
    std::string s;   // String: the text · Number: the source token
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // insertion order kept; canonical() sorts

    Json() = default;
    static Json null() { return Json(); }
    static Json boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
    static Json integer(int64_t v) { Json j; j.type = Int; j.i = v; return j; }
    static Json string(std::string v) { Json j; j.type = String; j.s = std::move(v); return j; }
    static Json array() { Json j; j.type = Array; return j; }
    static Json object() { Json j; j.type = Object; return j; }
    static Json strings(const std::vector<std::string>& v) { Json j = array(); for (const auto& x : v) j.arr.push_back(string(x)); return j; }

    bool is_null() const { return type == Null; }
    bool is_int() const { return type == Int; }
    bool is_str() const { return type == String; }
    bool is_arr() const { return type == Array; }
    bool is_obj() const { return type == Object; }

    const Json* get(const std::string& key) const;     // Object member or nullptr
    Json& set(const std::string& key, Json v);          // replace or append; returns *this
    Json& push(Json v) { arr.push_back(std::move(v)); return *this; }
    std::string str(const std::string& key, const std::string& d = "") const;   // member as string, else d
    int64_t num(const std::string& key, int64_t d = 0) const;                    // member as int, else d
    bool flag(const std::string& key, bool d = false) const;
    std::vector<std::string> str_list(const std::string& key) const;             // member array of strings

    std::string canonical() const;   // the reference's bytes
    std::string pretty(int indent = 0) const;   // for humans (2-space, keys in insertion order)
    bool operator==(const Json& o) const;
};

// Strict RFC 8259 parse of the whole text (trailing whitespace allowed). Returns false with *err set.
bool json_parse(const std::string& text, Json& out, std::string* err = nullptr);
std::string json_escape(const std::string& s);   // the canonical string escaping, without the quotes
std::string json_quote(const std::string& s);    // with the quotes

}  // namespace caseclock
