// caseclock · json.cpp — see json.h.
#include "json.h"

#include "app_util.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace caseclock {

const Json* Json::get(const std::string& key) const {
    if (type != Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}
Json& Json::set(const std::string& key, Json v) {
    type = Object;
    for (auto& kv : obj)
        if (kv.first == key) { kv.second = std::move(v); return *this; }
    obj.emplace_back(key, std::move(v));
    return *this;
}
std::string Json::str(const std::string& key, const std::string& d) const {
    const Json* v = get(key);
    return v && v->type == String ? v->s : d;
}
int64_t Json::num(const std::string& key, int64_t d) const {
    const Json* v = get(key);
    if (!v) return d;
    if (v->type == Int) return v->i;
    if (v->type == Number) return (int64_t)strtod(v->s.c_str(), nullptr);
    return d;
}
bool Json::flag(const std::string& key, bool d) const {
    const Json* v = get(key);
    return v && v->type == Bool ? v->b : d;
}
std::vector<std::string> Json::str_list(const std::string& key) const {
    std::vector<std::string> out;
    const Json* v = get(key);
    if (!v || v->type != Array) return out;
    for (const auto& x : v->arr)
        if (x.type == String) out.push_back(x.s);
    return out;
}
bool Json::operator==(const Json& o) const {
    if (type != o.type) return false;
    switch (type) {
        case Null: return true;
        case Bool: return b == o.b;
        case Int: return i == o.i;
        case Number: case String: return s == o.s;
        case Array: return arr == o.arr;
        case Object: {
            if (obj.size() != o.obj.size()) return false;
            for (const auto& kv : obj) {
                const Json* v = o.get(kv.first);
                if (!v || !(*v == kv.second)) return false;
            }
            return true;
        }
    }
    return false;
}

// ── writer ──────────────────────────────────────────────────────────────────
std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) o += ssprintf("\\u%04x", c);
                else o += (char)c;
        }
    }
    return o;
}
std::string json_quote(const std::string& s) { return "\"" + json_escape(s) + "\""; }

static void write_canonical(const Json& v, std::string& o) {
    switch (v.type) {
        case Json::Null: o += "null"; break;
        case Json::Bool: o += v.b ? "true" : "false"; break;
        case Json::Int: o += std::to_string(v.i); break;
        case Json::Number: o += v.s; break;
        case Json::String: o += '"'; o += json_escape(v.s); o += '"'; break;
        case Json::Array:
            o += '[';
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) o += ',';
                write_canonical(v.arr[i], o);
            }
            o += ']';
            break;
        case Json::Object: {
            std::vector<const std::pair<std::string, Json>*> kv;
            kv.reserve(v.obj.size());
            for (const auto& p : v.obj) kv.push_back(&p);
            std::stable_sort(kv.begin(), kv.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
            o += '{';
            for (size_t i = 0; i < kv.size(); ++i) {
                if (i) o += ',';
                o += '"'; o += json_escape(kv[i]->first); o += "\":";
                write_canonical(kv[i]->second, o);
            }
            o += '}';
            break;
        }
    }
}
std::string Json::canonical() const {
    std::string o;
    write_canonical(*this, o);
    return o;
}

static void write_pretty(const Json& v, std::string& o, int ind) {
    const std::string pad((size_t)ind * 2, ' '), pad2((size_t)(ind + 1) * 2, ' ');
    switch (v.type) {
        case Json::Array:
            if (v.arr.empty()) { o += "[]"; return; }
            o += "[\n";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                o += pad2;
                write_pretty(v.arr[i], o, ind + 1);
                o += i + 1 < v.arr.size() ? ",\n" : "\n";
            }
            o += pad + "]";
            return;
        case Json::Object:
            if (v.obj.empty()) { o += "{}"; return; }
            o += "{\n";
            for (size_t i = 0; i < v.obj.size(); ++i) {
                o += pad2 + json_quote(v.obj[i].first) + ": ";
                write_pretty(v.obj[i].second, o, ind + 1);
                o += i + 1 < v.obj.size() ? ",\n" : "\n";
            }
            o += pad + "}";
            return;
        default: write_canonical(v, o);
    }
}
std::string Json::pretty(int indent) const {
    std::string o;
    write_pretty(*this, o, indent);
    return o;
}

// ── parser ──────────────────────────────────────────────────────────────────
namespace {
struct P {
    const std::string& in;
    size_t i = 0;
    std::string err;
    explicit P(const std::string& s) : in(s) {}
    bool fail(const char* what) { if (err.empty()) err = ssprintf("%s at offset %zu", what, i); return false; }
    void ws() { while (i < in.size() && (in[i] == ' ' || in[i] == '\t' || in[i] == '\r' || in[i] == '\n')) ++i; }
    bool lit(const char* w, size_t n) {
        if (in.compare(i, n, w) != 0) return fail("bad literal");
        i += n;
        return true;
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > in.size()) return fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = in[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        if (i >= in.size() || in[i] != '"') return fail("expected string");
        ++i;
        out.clear();
        while (i < in.size()) {
            const unsigned char c = (unsigned char)in[i++];
            if (c == '"') return true;
            if (c < 0x20) return fail("control character in string");
            if (c != '\\') { out += (char)c; continue; }
            if (i >= in.size()) return fail("dangling escape");
            const char e = in[i++];
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
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= in.size() && in[i] == '\\' && in[i + 1] == 'u') {
                        const size_t save = i;
                        i += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else i = save;
                    }
                    utf8_put(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return fail("too deep");
        ws();
        if (i >= in.size()) return fail("unexpected end");
        const char c = in[i];
        if (c == '{') {
            out.type = Json::Object;
            ++i;
            ws();
            if (i < in.size() && in[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i >= in.size() || in[i] != ':') return fail("expected ':'");
                ++i;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.set(key, std::move(v));   // duplicate keys: the last wins, as Python
                ws();
                if (i < in.size() && in[i] == ',') { ++i; continue; }
                if (i < in.size() && in[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            out.type = Json::Array;
            ++i;
            ws();
            if (i < in.size() && in[i] == ']') { ++i; return true; }
            for (;;) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.arr.push_back(std::move(v));
                ws();
                if (i < in.size() && in[i] == ',') { ++i; continue; }
                if (i < in.size() && in[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { out.type = Json::String; return string(out.s); }
        if (c == 't') { out.type = Json::Bool; out.b = true; return lit("true", 4); }
        if (c == 'f') { out.type = Json::Bool; out.b = false; return lit("false", 5); }
        if (c == 'n') { out.type = Json::Null; return lit("null", 4); }
        // number
        const size_t start = i;
        if (in[i] == '-') ++i;
        if (i >= in.size() || !isdigit((unsigned char)in[i])) return fail("expected value");
        if (in[i] == '0') ++i;
        else while (i < in.size() && isdigit((unsigned char)in[i])) ++i;
        bool integer = true;
        if (i < in.size() && in[i] == '.') {
            integer = false;
            ++i;
            if (i >= in.size() || !isdigit((unsigned char)in[i])) return fail("bad fraction");
            while (i < in.size() && isdigit((unsigned char)in[i])) ++i;
        }
        if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
            integer = false;
            ++i;
            if (i < in.size() && (in[i] == '+' || in[i] == '-')) ++i;
            if (i >= in.size() || !isdigit((unsigned char)in[i])) return fail("bad exponent");
            while (i < in.size() && isdigit((unsigned char)in[i])) ++i;
        }
        const std::string tok = in.substr(start, i - start);
        if (integer) {
            errno = 0;
            char* end = nullptr;
            const long long v = strtoll(tok.c_str(), &end, 10);
            if (errno == ERANGE) return fail("integer out of range");
            out.type = Json::Int;
            out.i = v;
        } else {
            out.type = Json::Number;
            out.s = tok;
        }
        return true;
    }
};
}  // namespace

bool json_parse(const std::string& text, Json& out, std::string* err) {
    P p(text);
    out = Json();
    if (starts_with(text, "\xEF\xBB\xBF")) p.i = 3;   // a BOM from Notepad
    bool ok = p.value(out, 0);
    if (ok) {
        p.ws();
        if (p.i != text.size()) ok = p.fail("trailing characters");
    }
    if (!ok && err) *err = p.err;
    return ok;
}

}  // namespace caseclock
