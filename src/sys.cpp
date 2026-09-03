// caseclock · sys.cpp — see sys.h.
#include "sys.h"

#include "app_util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace caseclock {

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::string last_error() {
    const DWORD e = GetLastError();
    wchar_t* msg = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e,
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, nullptr);
    std::string s = n && msg ? narrow(std::wstring(msg, n)) : ssprintf("error %lu", (unsigned long)e);
    if (msg) LocalFree(msg);
    return rstrip(s);
}

bool read_file(const std::string& path, std::string& out, std::string* err) {
    HANDLE f = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { if (err) *err = path + ": " + last_error(); return false; }
    out.clear();
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) out.append(buf, got);
    CloseHandle(f);
    return true;
}
static bool write_all(HANDLE f, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        DWORD put = 0;
        if (!WriteFile(f, data.data() + off, (DWORD)std::min<size_t>(data.size() - off, 1u << 30), &put, nullptr)) return false;
        off += put;
    }
    return true;
}
bool write_file(const std::string& path, const std::string& data, std::string* err) {
    HANDLE f = CreateFileW(widen(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { if (err) *err = path + ": " + last_error(); return false; }
    const bool ok = write_all(f, data);
    if (!ok && err) *err = path + ": " + last_error();
    CloseHandle(f);
    return ok;
}
bool append_file(const std::string& path, const std::string& data, std::string* err) {
    HANDLE f = CreateFileW(widen(path).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { if (err) *err = path + ": " + last_error(); return false; }
    const bool ok = write_all(f, data);
    if (!ok && err) *err = path + ": " + last_error();
    CloseHandle(f);
    return ok;
}
bool file_exists(const std::string& path) {
    const DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool remove_file(const std::string& path) { return DeleteFileW(widen(path).c_str()) != 0; }
uint64_t file_size(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &fa)) return 0;
    return ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}
bool make_dirs(const std::string& path) {
    std::wstring w = widen(path);
    for (size_t i = 3; i <= w.size(); ++i) {
        if (i == w.size() || w[i] == L'\\' || w[i] == L'/') {
            const std::wstring part = w.substr(0, i);
            if (part.empty()) continue;
            const DWORD a = GetFileAttributesW(part.c_str());
            if (a == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
    }
    return true;
}

std::string exe_path() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return narrow(std::wstring(buf, n));
}
std::string exe_dir() { return dir_name(exe_path()); }
std::string local_app_data() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return n ? narrow(std::wstring(buf, n)) : std::string();
}
std::string temp_dir() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetTempPathW((DWORD)(sizeof buf / sizeof buf[0]), buf);
    return n ? narrow(std::wstring(buf, n)) : std::string();
}
std::string current_dir() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetCurrentDirectoryW((DWORD)(sizeof buf / sizeof buf[0]), buf);
    std::string s = n ? narrow(std::wstring(buf, n)) : std::string();
    if (!s.empty() && s.back() != '\\' && s.back() != '/') s += '\\';
    return s;
}
std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '\\' || a.back() == '/') return a + b;
    return a + "\\" + b;
}
std::string base_name(const std::string& path) {
    const size_t p = path.find_last_of("\\/");
    return p == std::string::npos ? path : path.substr(p + 1);
}
std::string dir_name(const std::string& path) {
    const size_t p = path.find_last_of("\\/");
    return p == std::string::npos ? std::string() : path.substr(0, p + 1);
}
std::string absolute_path(const std::string& path) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetFullPathNameW(widen(path).c_str(), (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
    return n ? narrow(std::wstring(buf, n)) : path;
}
std::vector<std::string> list_files(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(path_join(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string name = narrow(fd.cFileName);
        if (suffix.empty() || ends_with(name, suffix)) out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}
std::string sanitize_file_name(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (c < 0x20 || strchr("<>:\"/\\|?*", (char)c)) ? '_' : (char)c;
    if (o.empty()) o = "case";
    return o;
}

// ── DPAPI ───────────────────────────────────────────────────────────────────
std::string base64_encode(const std::string& raw) {
    DWORD n = 0;
    if (!CryptBinaryToStringA((const BYTE*)raw.data(), (DWORD)raw.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n)) return "";
    std::string out((size_t)n, '\0');
    if (!CryptBinaryToStringA((const BYTE*)raw.data(), (DWORD)raw.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &n)) return "";
    out.resize(n);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}
bool base64_decode(const std::string& text, std::string& raw) {
    DWORD n = 0;
    if (!CryptStringToBinaryA(text.c_str(), (DWORD)text.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return false;
    raw.assign((size_t)n, '\0');
    if (!CryptStringToBinaryA(text.c_str(), (DWORD)text.size(), CRYPT_STRING_BASE64, (BYTE*)raw.data(), &n, nullptr, nullptr)) return false;
    raw.resize(n);
    return true;
}
static const char kEntropy[] = "caseclock tape row v1";
bool dpapi_protect(const std::string& clear, std::string& out_b64, std::string* err) {
    DATA_BLOB in{(DWORD)clear.size(), (BYTE*)clear.data()};
    DATA_BLOB ent{(DWORD)sizeof(kEntropy) - 1, (BYTE*)kEntropy};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"caseclock tape", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (err) *err = "DPAPI: " + last_error();
        return false;
    }
    out_b64 = base64_encode(std::string((const char*)out.pbData, out.cbData));
    LocalFree(out.pbData);
    return !out_b64.empty();
}
bool dpapi_unprotect(const std::string& in_b64, std::string& clear, std::string* err) {
    std::string raw;
    if (!base64_decode(in_b64, raw)) { if (err) *err = "DPAPI: the row is not base64"; return false; }
    DATA_BLOB in{(DWORD)raw.size(), (BYTE*)raw.data()};
    DATA_BLOB ent{(DWORD)sizeof(kEntropy) - 1, (BYTE*)kEntropy};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if (err) *err = "DPAPI: " + last_error() + " (a tape is readable only by the Windows user who wrote it)";
        return false;
    }
    clear.assign((const char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

// ── the wall clock ──────────────────────────────────────────────────────────
LocalTime local_now() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return LocalTime{st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond};
}
std::string local_now_text() {
    const LocalTime t = local_now();
    return ssprintf("%04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
}
bool parse_local_iso(const std::string& s, LocalTime& out) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    char sep = 0;
    if (sscanf(s.c_str(), "%4d-%2d-%2d%c%2d:%2d", &y, &mo, &d, &sep, &h, &mi) != 6) return false;
    if (sep != 'T' && sep != ' ') return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return false;
    out = LocalTime{y, mo, d, h, mi, 0};
    return true;
}
static bool local_to_filetime(const LocalTime& t, ULONGLONG& ft) {
    SYSTEMTIME loc{};
    loc.wYear = (WORD)t.year; loc.wMonth = (WORD)t.month; loc.wDay = (WORD)t.day;
    loc.wHour = (WORD)t.hour; loc.wMinute = (WORD)t.minute; loc.wSecond = (WORD)t.second;
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &loc, &utc)) return false;
    FILETIME f{};
    if (!SystemTimeToFileTime(&utc, &f)) return false;
    ft = ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
    return true;
}
bool minutes_since(const LocalTime& origin, int64_t& out) {
    ULONGLONG o = 0;
    if (!local_to_filetime(origin, o)) return false;
    FILETIME nf;
    GetSystemTimeAsFileTime(&nf);
    const ULONGLONG n = ((ULONGLONG)nf.dwHighDateTime << 32) | nf.dwLowDateTime;
    const long long ticks = (long long)n - (long long)o;   // 100 ns
    const long long per_min = 600000000ll;
    long long m = ticks / per_min;
    if (ticks < 0 && ticks % per_min != 0) m -= 1;   // floor
    out = m;
    return true;
}
double now_seconds() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}
void sleep_ms(int ms) { Sleep((DWORD)(ms < 0 ? 0 : ms)); }

std::string user_name() {
    // the environment, not GetUserNameW: that one lives in advapi32, which is not on the import list
    wchar_t buf[256];
    const DWORD n = GetEnvironmentVariableW(L"USERNAME", buf, 256);
    if (n && n < 256) return narrow(std::wstring(buf, n));
    return "coordinator";
}

// ── the import table, from the exe's own headers ────────────────────────────
std::vector<std::string> imported_dlls() {
    std::vector<std::string> out;
    const uint8_t* base = (const uint8_t*)GetModuleHandleW(nullptr);
    if (!base) return out;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return out;
    const IMAGE_DATA_DIRECTORY& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp.VirtualAddress) {
        const IMAGE_IMPORT_DESCRIPTOR* d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + imp.VirtualAddress);
        for (; d->Name; ++d) out.push_back((const char*)(base + d->Name));
    }
    const IMAGE_DATA_DIRECTORY& del = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (del.VirtualAddress) {
        const IMAGE_DELAYLOAD_DESCRIPTOR* d = (const IMAGE_DELAYLOAD_DESCRIPTOR*)(base + del.VirtualAddress);
        for (; d->DllNameRVA; ++d) out.push_back(std::string((const char*)(base + d->DllNameRVA)) + " (delay)");
    }
    for (auto& s : out) std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    std::sort(out.begin(), out.end());
    return out;
}

bool is_console_stdout() {
    DWORD m;
    return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &m) != 0;
}
void console_utf8() {
    if (is_console_stdout()) SetConsoleOutputCP(CP_UTF8);
}
void stdout_binary() { _setmode(_fileno(stdout), _O_BINARY); }
void attach_parent_console() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
}
bool fresh_own_console() {
    if (GetConsoleWindow() != nullptr) {
        DWORD pids[4];
        return GetConsoleProcessList(pids, 4) <= 1;
    }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    return !h || h == INVALID_HANDLE_VALUE;
}
void free_console() { FreeConsole(); }
void write_out(const std::string& s) {
    fwrite(s.data(), 1, s.size(), stdout);
    fflush(stdout);
}

}  // namespace caseclock
