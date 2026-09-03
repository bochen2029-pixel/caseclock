// caseclock · sys.h — the OS: files, paths, DPAPI, the wall clock, the exe's own import table.
// Everything that touches windows.h and is not the window lives here, so the floor modules
// (closure, json, tape, casefile, clock, signout) stay pure C++ and testable in isolation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace caseclock {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);

bool read_file(const std::string& path, std::string& out, std::string* err = nullptr);
bool write_file(const std::string& path, const std::string& data, std::string* err = nullptr);    // create or truncate
bool append_file(const std::string& path, const std::string& data, std::string* err = nullptr);   // create if absent; one write
bool file_exists(const std::string& path);
bool remove_file(const std::string& path);
bool make_dirs(const std::string& path);   // mkdir -p
uint64_t file_size(const std::string& path);   // 0 if absent

std::string exe_path();      // UTF-8
std::string exe_dir();       // with the trailing separator
std::string local_app_data();   // %LOCALAPPDATA%
std::string temp_dir();
std::string current_dir();
std::string path_join(const std::string& a, const std::string& b);
std::string base_name(const std::string& path);
std::string dir_name(const std::string& path);   // with the trailing separator ("" if none)
std::string absolute_path(const std::string& path);
std::vector<std::string> list_files(const std::string& dir, const std::string& suffix);   // names, sorted
std::string sanitize_file_name(const std::string& s);   // a case id as a file name

// DPAPI (crypt32), per user, base64 text in and out so a row stays one line.
bool dpapi_protect(const std::string& clear, std::string& out_b64, std::string* err = nullptr);
bool dpapi_unprotect(const std::string& in_b64, std::string& clear, std::string* err = nullptr);
std::string base64_encode(const std::string& raw);
bool base64_decode(const std::string& text, std::string& raw);

// the wall clock
struct LocalTime { int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0; };
LocalTime local_now();
std::string local_now_text();   // "2026-09-03 04:40"
bool parse_local_iso(const std::string& s, LocalTime& out);   // "YYYY-MM-DDTHH:MM" or "YYYY-MM-DD HH:MM"
// whole minutes from `origin` (a local time) to now, floored; false if origin is invalid
bool minutes_since(const LocalTime& origin, int64_t& out);
double now_seconds();   // monotonic, for measurements
void sleep_ms(int ms);

std::string user_name();
std::vector<std::string> imported_dlls();   // the running exe's import table, read from its own headers
bool is_console_stdout();
void console_utf8();   // console output in UTF-8 (no-op when redirected)
void stdout_binary();  // LF-only: tapes, JSON and text pipe cleanly
void attach_parent_console();   // caseclockw.exe (no console): borrow the parent's for text modes
bool fresh_own_console();       // a real double-click launch (our own fresh console, nobody else in it)
void free_console();
void write_out(const std::string& s);   // stdout, whole buffer

}  // namespace caseclock
