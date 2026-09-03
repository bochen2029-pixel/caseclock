// caseclock · app_util.h — shared app-side helpers: options, formatting, UTF-8 widths.
// Pure std; no windows.h here. Formatting follows C:\facet\app_util.h so the tools read alike.
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace caseclock {

constexpr const char* kVersion = "0.1.0";

struct Opts {
    enum class Mode { Auto, Gui, Report, Signout, Explain, Json, Replay, Mcp, Spool, About, Selftest,
                      Fact, Export, Verify, Help, Version };
    Mode mode = Mode::Auto;
    std::vector<std::string> cases;        // case files, in order (UTF-8 paths)
    std::vector<std::string> rules;        // --rules PACK.json, in order
    std::string explain_event;             // --explain EVENT
    std::string fact_event, fact_time;     // --fact EVENT TIME
    std::string by, next;                  // --by NAME · --next NAME (sign-out header, fact author)
    std::string tape;                      // --tape PATH: the tape file (default %LOCALAPPDATA%\caseclock\<id>.tape.jsonl)
    std::string export_path;               // --export [FILE] (empty = stdout)
    std::string root;                      // --selftest --root DIR (where floor/ and tests/ live)
    std::string only;                      // --only CASE: pin the strip to one case id
    std::string shot;                      // --shot FILE.png: render the strip once, save, exit
    std::string log;                       // --log FILE (also CASECLOCK_LOG)
    bool has_now = false;
    int64_t now = 0;                       // --now T: minutes from the reference (or HH:MM [±Nd])
    bool clear = false;                    // --clear: the tape in the clear (no DPAPI) — replay, tests, export
    bool replay = false;                   // --replay: the case's facts at --speed on a scratch tape (console or --gui)
    bool all = false;                      // --json --all: every case
    bool no_activate = false;              // --no-activate: never take the keyboard
    bool no_keys = false;                  // --no-keys: hide the strip's key line
    bool json = false;
    bool plain = false;
    bool quiet = false;
    int speed = 60;                        // --replay --speed N
    int frames = 0;                        // --spool --frames N (0 = until Ctrl+C)
    int interval_ms = 2000;                // --spool poll interval
    std::string spool_file;                // --spool-file P
    std::string lane = "case";             // --lane L
};

inline std::string ssprintf(const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    char buf[4096];
    const int n = vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf, n > 0 ? (size_t)std::min<int>(n, sizeof(buf) - 1) : 0);
}

// ---- UTF-8 ----
inline uint32_t utf8_next(std::string_view s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    const int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = (n == 3) ? (c & 0x07u) : (n == 2) ? (c & 0x0Fu) : (n == 1) ? (c & 0x1Fu) : c;
    for (int k = 0; k < n && i < s.size() && (((unsigned char)s[i]) & 0xC0) == 0x80; ++k, ++i)
        cp = (cp << 6) | (((unsigned char)s[i]) & 0x3Fu);
    return cp;
}
inline void utf8_put(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}
// code points, which is what Python's str.__len__ and f"{s:<62}" count
inline size_t cp_count(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) { utf8_next(s, i); ++n; }
    return n;
}
// Python f"{s:<N}": pad on the right to N code points
inline std::string pad_cp(const std::string& s, size_t n) {
    const size_t w = cp_count(s);
    return w >= n ? s : s + std::string(n - w, ' ');
}
// Python f"{s:>N}": pad on the left to N code points
inline std::string rpad_cp(const std::string& s, size_t n) {
    const size_t w = cp_count(s);
    return w >= n ? s : std::string(n - w, ' ') + s;
}
inline std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r' || s.back() == '\f' || s.back() == '\v'))
        s.pop_back();
    return s;
}
inline int cp_width(uint32_t cp) {
    if (cp == 0 || cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if ((cp >= 0x300 && cp <= 0x36F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}
inline int display_width(std::string_view s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) w += cp_width(utf8_next(s, i));
    return w;
}
inline std::string pad_display(const std::string& s, int cols) {
    const int w = display_width(s);
    return w >= cols ? s : s + std::string((size_t)(cols - w), ' ');
}
inline std::string rpad_display(const std::string& s, int cols) {
    const int w = display_width(s);
    return w >= cols ? s : std::string((size_t)(cols - w), ' ') + s;
}
inline std::string hex_short(const std::string& hex, size_t n = 4) {
    if (hex.size() <= 2 * n) return hex;
    return hex.substr(0, n) + "\xE2\x80\xA6" + hex.substr(hex.size() - 2);   // "a3f9…c1"
}
inline std::string to_hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string o;
    o.reserve(2 * n);
    for (size_t i = 0; i < n; ++i) { o += d[p[i] >> 4]; o += d[p[i] & 15]; }
    return o;
}
inline bool starts_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
inline bool ends_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

}  // namespace caseclock
