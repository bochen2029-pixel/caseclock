// caseclock · strip.cpp — the window: facet's status bar with the rail and the table removed. One
// row, 28 logical px, docked to the bottom of the primary monitor, always on top, showing the nearest
// deadline across every loaded case; a 12-px key line under it (house rule four: the shortcuts are
// printed); panes under it for a fact (Ctrl+L), the chain (Ctrl+E), the sign-out (Ctrl+S), a case
// (Ctrl+O). The left mark is the state: dim, amber, red, violet — and the text always carries it.
//
// Test seam (as facet): CASECLOCK_LOG=FILE / --log FILE logs every keystroke, fact, closure and strip
// state; WM_COPYDATA with dwData = WM_APP+7 and a UTF-8 "event=E;minutes=M;by=B" payload posts a fact
// from a driver; WM_APP+7 itself (wParam = minutes, lParam = index into the watched events) does the
// same without a payload; --shot FILE.png renders once and exits; --no-activate never takes the keyboard.
#include "app_util.h"
#include "clock.h"
#include "closure.h"
#include "signout.h"
#include "sys.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>   // IStream, which gdiplus.h needs under WIN32_LEAN_AND_MEAN
#include <windowsx.h>
#include <shellapi.h>
#define PSAPI_VERSION 2   // K32GetProcessMemoryInfo from kernel32: the working set for the log, no psapi import
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <memory>
using std::max;   // gdiplus.h spells the macros; NOMINMAX took them away
using std::min;
#include <gdiplus.h>

namespace caseclock {
namespace {

constexpr COLORREF kBg = RGB(18, 18, 24), kPanel = RGB(26, 26, 34), kLine = RGB(58, 58, 70), kText = RGB(232, 232, 238),
                   kDim = RGB(150, 150, 162), kQuiet = RGB(92, 92, 104), kAmber = RGB(244, 180, 66), kRed = RGB(236, 84, 84),
                   kViolet = RGB(176, 128, 240), kEditBg = RGB(38, 38, 50), kGreen = RGB(72, 190, 110);

enum class Pane { None, Fact, Explain, Signout, Open };

struct Gui {
    Opts opts;
    std::vector<std::unique_ptr<CaseRuntime>>* cases = nullptr;
    std::vector<int64_t> launch_minute;   // each case's minute at launch (the clock advances from it)
    double launch_seconds = 0;
    size_t sel = 0;
    bool sel_pinned = false;   // --only or Ctrl+Tab: the strip stays on one case
    HWND hwnd = nullptr;
    int dpi = 96;
    HFONT fMain = nullptr, fSmall = nullptr, fMono = nullptr;
    bool topmost = true, show_keys = true, dock_top = false;
    int width_pct = 100;
    Pane pane = Pane::None;
    std::string pane_text;      // explain / sign-out, UTF-8
    std::string explain_event;  // the event the explain pane shows
    int scroll = 0;             // pane: first visible line
    HWND edit1 = nullptr, edit2 = nullptr;
    WNDPROC edit_proc = nullptr;
    HBRUSH edit_brush = nullptr;
    std::wstring status;        // a transient line ("sign-out copied")
    double status_until = 0;
    int esc_count = 0;
    double esc_at = 0;
    bool hidden = false;        // in the tray
    NOTIFYICONDATAW nid{};
    HICON icon = nullptr;
    bool dragging = false;
    POINT drag_start{};
    RECT drag_rc{};
    bool shot_pending = false;
    RECT rcStrip{}, rcKeys{}, rcPane{};
    std::string ini;
    std::string last_line;      // for the log
    double paint_us = 0;        // the last paint, measured
};
Gui* g = nullptr;

constexpr UINT WM_APP_FACT = WM_APP + 7;
constexpr UINT WM_TRAY = WM_APP + 8;
constexpr UINT_PTR kTickTimer = 1, kShotTimer = 2, kStatusTimer = 3;
constexpr int kEditId = 100, kEdit2Id = 101;
enum { kMenuShow = 1, kMenuPin, kMenuKeys, kMenuSignout, kMenuQuit };

// ---- the seam ----
void dbg(const char* fmt, ...) {
    static FILE* f = nullptr;
    static bool init = false;
    if (!init) {
        init = true;
        std::string path = g ? g->opts.log : "";
        if (path.empty()) {
            wchar_t buf[MAX_PATH];
            const DWORD n = GetEnvironmentVariableW(L"CASECLOCK_LOG", buf, MAX_PATH);
            if (n && n < MAX_PATH) path = narrow(std::wstring(buf, n));
        }
        if (!path.empty()) f = _wfopen(widen(path).c_str(), L"a");
    }
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}

int px(int v) { return MulDiv(v, g->dpi, 96); }

void make_fonts(Gui& s) {
    for (HFONT* f : {&s.fMain, &s.fSmall, &s.fMono})
        if (*f) { DeleteObject(*f); *f = nullptr; }
    auto mk = [&](int tenths_pt, int weight, const wchar_t* face) {
        return CreateFontW(-MulDiv(tenths_pt, s.dpi, 720), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    };
    s.fMain = mk(110, FW_SEMIBOLD, L"Segoe UI");
    s.fSmall = mk(80, FW_NORMAL, L"Segoe UI");
    s.fMono = mk(95, FW_NORMAL, L"Consolas");
    if (s.edit1) SendMessageW(s.edit1, WM_SETFONT, (WPARAM)s.fMain, TRUE);
    if (s.edit2) SendMessageW(s.edit2, WM_SETFONT, (WPARAM)s.fMain, TRUE);
}

// ---- gdi ----
void fill(HDC dc, const RECT& rc, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}
RECT rect(int l, int t, int r, int b) { RECT rc{l, t, r, b}; return rc; }
void draw_text(HDC dc, HFONT f, COLORREF c, RECT rc, const std::wstring& t, UINT flags) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, t.c_str(), (int)t.size(), &rc, flags | DT_NOPREFIX);
    SelectObject(dc, of);
}
int text_width(HDC dc, HFONT f, const std::wstring& t) {
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &sz);
    SelectObject(dc, of);
    return sz.cx;
}
int line_height(HDC dc, HFONT f) {
    HGDIOBJ of = SelectObject(dc, f);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    return tm.tmHeight + tm.tmExternalLeading;
}

// ---- the icon: a clock square in the mood's colour ----
HICON make_icon(int sz, COLORREF mood) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) return nullptr;
    std::vector<uint32_t> p((size_t)sz * (size_t)sz, 0xFF14141A);
    const uint32_t m = 0xFF000000u | ((uint32_t)GetRValue(mood) << 16) | ((uint32_t)GetGValue(mood) << 8) | GetBValue(mood);
    const int gp = std::max(1, sz / 8);
    for (int y = gp; y < sz - gp; ++y)
        for (int x = gp; x < gp + std::max(2, sz / 5); ++x) p[(size_t)y * sz + x] = m;   // the left mark
    for (int y = sz / 2 - 1; y <= sz / 2; ++y)
        for (int x = gp + sz / 4; x < sz - gp; ++x) p[(size_t)y * sz + x] = 0xFFE8E8EE;   // the line
    memcpy(bits, p.data(), p.size() * 4);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return h;
}

// ---- ini ----
void ini_load(Gui& s) {
    std::string text;
    if (!read_file(s.ini, text)) return;
    size_t p = 0;
    while (p < text.size()) {
        size_t q = text.find('\n', p);
        if (q == std::string::npos) q = text.size();
        std::string ln = rstrip(text.substr(p, q - p));
        p = q + 1;
        const size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
        if (k == "top") s.dock_top = v == "1";
        else if (k == "keys") s.show_keys = v != "0";
        else if (k == "topmost") s.topmost = v != "0";
        else if (k == "width_pct") s.width_pct = std::clamp((int)strtol(v.c_str(), nullptr, 10), 30, 100);
    }
}
void ini_save(const Gui& s) {
    write_file(s.ini, ssprintf("top=%d\nkeys=%d\ntopmost=%d\nwidth_pct=%d\n", s.dock_top ? 1 : 0, s.show_keys ? 1 : 0, s.topmost ? 1 : 0, s.width_pct));
}

// ---- the clock ----
int64_t case_minute(Gui& s, size_t i) {
    CaseRuntime& rt = *(*s.cases)[i];
    const double elapsed = now_seconds() - s.launch_seconds;
    if (rt.replaying) {
        const int speed = std::max(0, s.opts.speed);
        int64_t m = rt.replay_start + (int64_t)std::floor(elapsed * speed / 60.0);
        return std::min(m, rt.replay_end);
    }
    if (!rt.doc.reference_at.empty() && !s.opts.has_now) {
        LocalTime t;
        int64_t m = 0;
        if (parse_local_iso(rt.doc.reference_at, t) && minutes_since(t, m)) return m;
    }
    return s.launch_minute[i] + (int64_t)std::floor(elapsed / 60.0);
}

size_t auto_case(const Gui& s) {
    // the case with the most urgent state: infeasible first, then the smallest slack, then the first
    size_t best = 0;
    int64_t best_slack = INF;
    bool best_inf = false;
    for (size_t i = 0; i < s.cases->size(); ++i) {
        const CaseRuntime& rt = *(*s.cases)[i];
        if (rt.infeasible()) { if (!best_inf) { best = i; best_inf = true; best_slack = -INF; } continue; }
        Deadline d;
        if (rt.nearest(d) && d.slack < best_slack && !best_inf) { best = i; best_slack = d.slack; }
    }
    return best;
}
CaseRuntime& shown(Gui& s) {
    if (!s.sel_pinned) s.sel = auto_case(s);
    if (s.sel >= s.cases->size()) s.sel = 0;
    return *(*s.cases)[s.sel];
}
int open_total(const Gui& s) {
    int n = 0;
    for (const auto& c : *s.cases) n += (int)c->open().size();
    return n;
}
COLORREF mood_color(CaseRuntime::Mood m) {
    switch (m) {
        case CaseRuntime::Mood::Amber: return kAmber;
        case CaseRuntime::Mood::Red: return kRed;
        case CaseRuntime::Mood::Violet: return kViolet;
        default: return kQuiet;
    }
}

void set_status(Gui& s, const std::wstring& t, double seconds = 4.0) {
    s.status = t;
    s.status_until = now_seconds() + seconds;
    SetTimer(s.hwnd, kStatusTimer, (UINT)(seconds * 1000), nullptr);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

// ---- layout ----
std::vector<std::string> split_lines(const std::string& t) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= t.size()) {
        size_t q = t.find('\n', p);
        if (q == std::string::npos) q = t.size();
        out.push_back(t.substr(p, q - p));
        p = q + 1;
        if (q == t.size()) break;
    }
    return out;
}

int pane_height(Gui& s) {
    if (s.pane == Pane::None) return 0;
    if (s.pane == Pane::Fact || s.pane == Pane::Open) return px(96);
    HDC dc = GetDC(s.hwnd);
    const int lh = line_height(dc, s.fMono);
    ReleaseDC(s.hwnd, dc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int lines = (int)split_lines(s.pane_text).size();
    const int want = lines * lh + px(16);
    return std::clamp(want, px(80), (int)(wa.bottom - wa.top) * 7 / 10);
}

void place(Gui& s) {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int stripH = px(28), keysH = s.show_keys ? px(12) : 0, paneH = pane_height(s);
    const int h = stripH + keysH + paneH;
    const int fullW = wa.right - wa.left;
    const int w = fullW * s.width_pct / 100;
    const int x = wa.left + (fullW - w) / 2;
    const int y = s.dock_top ? wa.top : wa.bottom - h;
    SetWindowPos(s.hwnd, s.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);
    RECT c;
    GetClientRect(s.hwnd, &c);
    // docked at the bottom the pane opens upward: strip at the bottom of the window; at the top, downward
    if (s.dock_top) {
        s.rcStrip = rect(0, 0, c.right, stripH);
        s.rcKeys = rect(0, stripH, c.right, stripH + keysH);
        s.rcPane = rect(0, stripH + keysH, c.right, c.bottom);
    } else {
        s.rcPane = rect(0, 0, c.right, paneH);
        s.rcStrip = rect(0, paneH, c.right, paneH + stripH);
        s.rcKeys = rect(0, paneH + stripH, c.right, c.bottom);
    }
    if (s.edit1) {
        const int m = px(12), eh = px(26), label = px(70);
        const int top = s.rcPane.top + px(34);
        if (s.pane == Pane::Fact) {
            const int half = (c.right - 2 * m) / 2;
            MoveWindow(s.edit1, m + label, top, half - label - px(8), eh, TRUE);
            if (s.edit2) MoveWindow(s.edit2, m + half + label, top, half - label - px(8), eh, TRUE);
        } else {
            MoveWindow(s.edit1, m + label, top, c.right - 2 * m - label, eh, TRUE);
        }
    }
}

// ---- panes ----
void close_pane(Gui& s) {
    if (s.edit1) { DestroyWindow(s.edit1); s.edit1 = nullptr; }
    if (s.edit2) { DestroyWindow(s.edit2); s.edit2 = nullptr; }
    s.pane = Pane::None;
    s.pane_text.clear();
    s.scroll = 0;
    place(s);
    SetFocus(s.hwnd);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK edit_sub(HWND h, UINT m, WPARAM wp, LPARAM lp);

HWND make_edit(Gui& s, int id, const std::wstring& text) {
    HWND e = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 10, 10, s.hwnd, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(nullptr), nullptr);
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)edit_sub);
    if (!s.edit_proc) s.edit_proc = prev;
    SendMessageW(e, WM_SETFONT, (WPARAM)s.fMain, TRUE);
    SendMessageW(e, EM_SETLIMITTEXT, 400, 0);
    return e;
}

void open_fact_pane(Gui& s) {
    close_pane(s);
    s.pane = Pane::Fact;
    CaseRuntime& rt = shown(s);
    Deadline d;
    s.edit1 = make_edit(s, kEditId, rt.nearest(d) ? widen(d.event) : L"");
    s.edit2 = make_edit(s, kEdit2Id, L"");
    place(s);
    SetFocus(s.edit1);
    SendMessageW(s.edit1, EM_SETSEL, 0, -1);
    InvalidateRect(s.hwnd, nullptr, FALSE);
    dbg("pane fact");
}
void open_explain_pane(Gui& s, const std::string& event) {
    close_pane(s);
    CaseRuntime& rt = shown(s);
    s.pane = Pane::Explain;
    s.explain_event = event;
    if (rt.infeasible()) s.pane_text = rt.strip_text() + "\n\n" + "these constraints cannot all hold:\n";
    else s.pane_text = rt.explain(event) + "\n";
    if (rt.infeasible())
        for (const Constraint& c : rt.closure.cycle_constraints(rt.closure.negative_cycle())) s.pane_text += "    " + c.render() + "\n";
    else if (rt.closure.has(event)) s.pane_text += "\n" + render_chain(rt.closure.binding_path(event), "  ");
    s.pane_text += "\nUp/Down: another deadline \xC2\xB7 Esc closes";
    place(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
    dbg("pane explain %s", event.c_str());
}
bool copy_text(HWND h, const std::wstring& t) {
    if (!OpenClipboard(h)) return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) { memcpy(p, t.c_str(), (t.size() + 1) * sizeof(wchar_t)); GlobalUnlock(mem); ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr; }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}
void open_signout_pane(Gui& s) {
    close_pane(s);
    CaseRuntime& rt = shown(s);
    s.pane = Pane::Signout;
    const std::string by = rt.by.empty() ? user_name() : rt.by;
    const std::string text = render_signout(rt, by, "on-call");
    std::string err;
    if (!rt.record_signout(text, by, "on-call", &err)) set_status(s, L"could not write the tape: " + widen(err), 8);
    s.pane_text = text + "\nCtrl+P prints \xC2\xB7 Esc closes";
    const bool copied = copy_text(s.hwnd, widen(text));
    place(s);
    set_status(s, copied ? L"sign-out copied to the clipboard and recorded on the tape" : L"sign-out recorded on the tape (clipboard busy)");
    dbg("pane signout rows=%zu head=%s", rt.tape.size(), rt.tape.head().c_str());
}
void print_signout(Gui& s) {
    CaseRuntime& rt = shown(s);
    const std::string by = rt.by.empty() ? user_name() : rt.by;
    const std::string text = s.pane == Pane::Signout ? s.pane_text.substr(0, s.pane_text.rfind("\nCtrl+P")) : render_signout(rt, by, "on-call");
    const std::string dir = path_join(default_tape_dir(), "print");
    make_dirs(dir);
    const std::string path = path_join(dir, sanitize_file_name(rt.doc.id) + "-" + sanitize_file_name(hhmm(rt.now)) + ".txt");
    std::string err;
    if (!write_file(path, "\xEF\xBB\xBF" + text, &err)) { set_status(s, L"could not write " + widen(path)); return; }
    const HINSTANCE r = ShellExecuteW(s.hwnd, L"print", widen(path).c_str(), nullptr, nullptr, SW_HIDE);
    set_status(s, (INT_PTR)r > 32 ? L"printing " + widen(path) + L" (the one clear-text copy; it is yours to shred)" : L"no print handler for .txt; the file is at " + widen(path), 8);
    dbg("print %s", path.c_str());
}
void open_case_pane(Gui& s) {
    close_pane(s);
    s.pane = Pane::Open;
    const std::string dir = s.cases->empty() ? current_dir() : dir_name(absolute_path((*s.cases)[0]->doc.path));
    s.edit1 = make_edit(s, kEditId, widen(dir));
    place(s);
    SetFocus(s.edit1);
    const LRESULT len = SendMessageW(s.edit1, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(s.edit1, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    InvalidateRect(s.hwnd, nullptr, FALSE);
    dbg("pane open");
}

std::wstring edit_text(HWND e) {
    wchar_t buf[512];
    const int n = GetWindowTextW(e, buf, 512);
    return std::wstring(buf, (size_t)std::max(0, n));
}

void submit_fact(Gui& s) {
    CaseRuntime& rt = shown(s);
    const std::string event = narrow(edit_text(s.edit1)), time = narrow(edit_text(s.edit2));
    int64_t minutes = 0;
    if (event.empty()) { set_status(s, L"a fact needs an event name"); SetFocus(s.edit1); return; }
    if (!parse_hhmm(time, minutes)) { set_status(s, L"time: HH:MM, HH:MM -1d, or minutes from the reference"); SetFocus(s.edit2); return; }
    const size_t before = rt.tape.size();
    std::string err;
    const int64_t at = case_minute(s, s.sel);
    if (!rt.add_fact(event, minutes, rt.by.empty() ? user_name() : rt.by, at, &err)) { set_status(s, L"could not write: " + widen(err), 8); return; }
    dbg("fact %s %lld by %s at %lld -> %zu rows", event.c_str(), (long long)minutes, rt.by.c_str(), (long long)at, rt.tape.size() - before);
    close_pane(s);
    set_status(s, widen(ssprintf("fact written: %s at %s (%zu rows on the tape)", event.c_str(), hhmm(minutes).c_str(), rt.tape.size())));
    if (s.icon) { DestroyIcon(s.icon); s.icon = nullptr; }
}
void submit_open(Gui& s) {
    const std::string path = narrow(edit_text(s.edit1));
    CaseDoc doc;
    std::string err;
    if (!load_case_file(path, doc, &err)) { set_status(s, L"cannot load: " + widen(err), 8); return; }
    if (doc.id.empty()) doc.id = base_name(path);
    for (const auto& c : *s.cases)
        if (c->doc.id == doc.id) { set_status(s, L"already loaded: " + widen(doc.id)); close_pane(s); return; }
    auto rt = std::make_unique<CaseRuntime>();
    rt->by = (*s.cases)[0]->by;
    int64_t now = doc.has_now ? doc.now : 0;
    if (!doc.reference_at.empty()) {
        LocalTime t;
        if (!parse_local_iso(doc.reference_at, t) || !minutes_since(t, now)) { set_status(s, L"reference_at is not a local YYYY-MM-DDTHH:MM"); return; }
    }
    if (!rt->load(doc, (*s.cases)[0]->packs, tape_path_for(doc.id), !s.opts.clear, now, &err)) { set_status(s, L"cannot load: " + widen(err), 8); return; }
    s.launch_minute.push_back(now);
    s.cases->push_back(std::move(rt));
    s.sel = s.cases->size() - 1;
    s.sel_pinned = true;
    close_pane(s);
    set_status(s, L"loaded " + widen(doc.id));
    dbg("open %s", path.c_str());
}

// ---- keys ----
void toggle_topmost(Gui& s) {
    s.topmost = !s.topmost;
    SetWindowPos(s.hwnd, s.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ini_save(s);
    set_status(s, s.topmost ? L"pinned on top" : L"unpinned");
}
void cycle_case(Gui& s) {
    if (s.cases->size() < 2) { set_status(s, L"one case loaded (Ctrl+O opens another)"); return; }
    s.sel = (s.sel + 1) % s.cases->size();
    s.sel_pinned = true;
    set_status(s, L"case " + widen((*s.cases)[s.sel]->doc.id) + widen(ssprintf(" (%zu of %zu)", s.sel + 1, s.cases->size())));
}
void to_tray(Gui& s) {
    s.hidden = true;
    ShowWindow(s.hwnd, SW_HIDE);
    dbg("tray hide");
}
void from_tray(Gui& s) {
    s.hidden = false;
    ShowWindow(s.hwnd, s.opts.no_activate ? SW_SHOWNOACTIVATE : SW_SHOW);
    place(s);
    dbg("tray show");
}
void on_escape(Gui& s) {
    if (s.pane != Pane::None) { close_pane(s); s.esc_count = 0; return; }
    const double t = now_seconds();
    if (s.esc_count == 1 && t - s.esc_at < 1.5) { s.esc_count = 0; to_tray(s); return; }
    s.esc_count = 1;
    s.esc_at = t;
    set_status(s, L"Esc again to hide in the tray (the clock keeps running)", 1.5);
}
bool handle_key(Gui& s, WPARAM wp, bool ctrl) {
    if (wp == VK_ESCAPE) { on_escape(s); return true; }
    if (ctrl && wp == 'L') { open_fact_pane(s); return true; }
    if (ctrl && wp == 'E') {
        CaseRuntime& rt = shown(s);
        Deadline d;
        open_explain_pane(s, rt.nearest(d) ? d.event : (rt.doc.explain.empty() ? std::string() : rt.doc.explain.front()));
        return true;
    }
    if (ctrl && wp == 'S') { open_signout_pane(s); return true; }
    if (ctrl && wp == 'P') { print_signout(s); return true; }
    if (ctrl && wp == 'O') { open_case_pane(s); return true; }
    if (ctrl && wp == 'T') { toggle_topmost(s); return true; }
    if (ctrl && wp == VK_TAB) { cycle_case(s); return true; }
    if (ctrl && wp == 'K') { s.show_keys = !s.show_keys; ini_save(s); place(s); return true; }
    if (s.pane == Pane::Explain && (wp == VK_UP || wp == VK_DOWN)) {
        CaseRuntime& rt = shown(s);
        const std::vector<Deadline> dl = rt.open();
        if (dl.empty()) return true;
        size_t i = 0;
        for (; i < dl.size(); ++i)
            if (dl[i].event == s.explain_event) break;
        if (i >= dl.size()) i = 0;
        else i = wp == VK_DOWN ? (i + 1) % dl.size() : (i + dl.size() - 1) % dl.size();
        open_explain_pane(s, dl[i].event);
        return true;
    }
    if ((s.pane == Pane::Explain || s.pane == Pane::Signout) && (wp == VK_PRIOR || wp == VK_NEXT)) {
        s.scroll = std::max(0, s.scroll + (wp == VK_NEXT ? 10 : -10));
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

LRESULT CALLBACK edit_sub(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Gui& s = *g;
    if (m == WM_KEYDOWN) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (wp == VK_RETURN) {
            if (s.pane == Pane::Open) submit_open(s);
            else if (h == s.edit1 && s.edit2) { SetFocus(s.edit2); SendMessageW(s.edit2, EM_SETSEL, 0, -1); }
            else submit_fact(s);
            return 0;
        }
        if (wp == VK_TAB && s.pane == Pane::Fact && h == s.edit1) {
            // completion: the first watched event that starts with what was typed
            const std::string typed = narrow(edit_text(h));
            for (const std::string& ev : shown(s).watched())
                if (starts_with(ev, typed)) { SetWindowTextW(h, widen(ev).c_str()); SendMessageW(h, EM_SETSEL, 0, -1); break; }
            return 0;
        }
        if (wp == VK_TAB && s.edit2) { SetFocus(h == s.edit1 ? s.edit2 : s.edit1); return 0; }
        if (wp == VK_ESCAPE || (ctrl && (wp == 'L' || wp == 'E' || wp == 'S' || wp == 'O' || wp == 'T' || wp == 'P' || wp == VK_TAB))) {
            if (handle_key(s, wp, ctrl)) return 0;
        }
        if (ctrl && wp == 'A') { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
    }
    if (m == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB || wp == 1)) return 0;   // no ding
    if (m == WM_KEYUP && s.pane == Pane::Fact && h == s.edit1) InvalidateRect(s.hwnd, &s.rcPane, FALSE);   // the completion hint
    return CallWindowProcW(s.edit_proc, h, m, wp, lp);
}

// ---- paint ----
void paint(Gui& s, HDC dc) {
    RECT c;
    GetClientRect(s.hwnd, &c);
    fill(dc, c, kBg);
    CaseRuntime& rt = shown(s);
    const int m = px(10);
    // the strip
    {
        const RECT& r = s.rcStrip;
        const COLORREF mood = mood_color(rt.mood());
        fill(dc, rect(r.left + m, r.top + px(6), r.left + m + px(4), r.bottom - px(6)), mood);
        std::string line = rt.strip_text();
        const int open = open_total(s);
        std::wstring right = widen(ssprintf("%d open", open));
        if (s.cases->size() > 1) right += widen(ssprintf("  \xC2\xB7  case %zu/%zu", s.sel + 1, s.cases->size()));
        if (rt.replaying) right += L"  \xB7  REPLAY " + widen(hhmm(rt.now));
        if (!s.status.empty()) right = s.status;
        const int rw = text_width(dc, s.fSmall, right) + m;
        draw_text(dc, s.fSmall, s.status.empty() ? kDim : kText, rect(r.right - rw - m, r.top, r.right - m, r.bottom), right, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_text(dc, s.fMain, kText, rect(r.left + m + px(12), r.top, r.right - rw - 2 * m, r.bottom), widen(line), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (line != s.last_line) { dbg("strip %s | %s", line.c_str(), narrow(right).c_str()); s.last_line = line; }
    }
    // the keys
    if (s.show_keys) {
        const RECT& r = s.rcKeys;
        fill(dc, r, kPanel);
        std::wstring keys = L"Ctrl+L fact \xB7 Ctrl+E explain \xB7 Ctrl+S sign-out \xB7 Ctrl+O case \xB7 Ctrl+T pin \xB7 Esc";
        if (s.cases->size() > 1) keys += L" \xB7 Ctrl+Tab case";
        draw_text(dc, s.fSmall, kDim, rect(r.left + m, r.top, r.right / 2, r.bottom), keys, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::string st = rt.layers_text() + ssprintf(" \xC2\xB7 tape %zu rows \xC2\xB7 %s", rt.tape.size(), rt.tape_path.empty() ? "in memory" : (rt.encrypt ? "DPAPI at rest" : "clear"));
        if (rt.doc.synthetic) st += " \xC2\xB7 SYNTHETIC";
        draw_text(dc, s.fSmall, kDim, rect(r.right / 2, r.top, r.right - m, r.bottom), widen(st), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    // the pane
    if (s.pane != Pane::None) {
        const RECT& r = s.rcPane;
        fill(dc, r, kPanel);
        fill(dc, s.dock_top ? rect(r.left, r.top, r.right, r.top + 1) : rect(r.left, r.bottom - 1, r.right, r.bottom), kLine);
        if (s.pane == Pane::Fact || s.pane == Pane::Open) {
            const int label = px(70), top = r.top + px(34), eh = px(26);
            draw_text(dc, s.fSmall, kDim, rect(r.left + px(12), r.top + px(8), r.right, r.top + px(30)),
                      s.pane == Pane::Fact ? L"A FACT \xB7 a time the coordinator knows \xB7 Enter writes the row and reruns the closure \xB7 Tab completes the event name"
                                           : L"OPEN A CASE \xB7 a case file (REGISTRAR JSON) \xB7 Enter loads it beside the others",
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (s.pane == Pane::Fact) {
                const int half = (r.right - 2 * px(12)) / 2;
                draw_text(dc, s.fSmall, kDim, rect(r.left + px(12), top, r.left + px(12) + label, top + eh), L"event", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                draw_text(dc, s.fSmall, kDim, rect(r.left + px(12) + half, top, r.left + px(12) + half + label, top + eh), L"time", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                // the completion hint: the watched events matching what was typed
                const std::string typed = s.edit1 ? narrow(edit_text(s.edit1)) : "";
                std::string hint;
                int n = 0;
                for (const std::string& ev : rt.watched())
                    if (starts_with(ev, typed)) { hint += (n++ ? "  " : "") + ev; if (n >= 8) break; }
                if (n == 0) hint = "no watched event starts with that; any name is accepted";
                draw_text(dc, s.fSmall, kDim, rect(r.left + px(12), top + eh + px(6), r.right - px(12), top + eh + px(26)), widen(hint), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            } else {
                draw_text(dc, s.fSmall, kDim, rect(r.left + px(12), top, r.left + px(12) + label, top + eh), L"path", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            const std::string ex = "time: 22:15, 22:15 -1d, or minutes from the reference (" + rt.doc.reference + ")";
            draw_text(dc, s.fSmall, kQuiet, rect(r.left + px(12), r.bottom - px(22), r.right - px(12), r.bottom - px(4)), widen(s.pane == Pane::Fact ? ex : "the tool watches nothing else: a case enters through this box or the command line"),
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            const std::vector<std::string> lines = split_lines(s.pane_text);
            const int lh = line_height(dc, s.fMono);
            int y = r.top + px(8);
            for (size_t i = (size_t)s.scroll; i < lines.size() && y + lh <= r.bottom; ++i, y += lh) {
                const bool head = starts_with(lines[i], "CASECLOCK") || starts_with(lines[i], "DUE,") || starts_with(lines[i], "SINCE") || starts_with(lines[i], "THE CHAIN") || starts_with(lines[i], "INFEASIBLE");
                draw_text(dc, s.fMono, head ? kText : kDim, rect(r.left + px(12), y, r.right - px(12), y + lh), widen(lines[i]), DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
            }
        }
    }
}

// ---- screenshot ----
bool png_clsid(CLSID& out) {
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    if (!sz) return false;
    std::vector<uint8_t> buf(sz);
    auto* enc = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(n, sz, enc);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(enc[i].MimeType, L"image/png") == 0) { out = enc[i].Clsid; return true; }
    return false;
}
bool save_shot(HWND hwnd, const std::wstring& path) {
    Gdiplus::GdiplusStartupInput in;
    ULONG_PTR tok = 0;
    if (Gdiplus::GdiplusStartup(&tok, &in, nullptr) != Gdiplus::Ok) return false;
    RECT rc;
    GetClientRect(hwnd, &rc);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    HDC wdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mdc, bmp);
    bool ok = PrintWindow(hwnd, mdc, PW_CLIENTONLY | 0x00000002 /* PW_RENDERFULLCONTENT */) != 0;
    SelectObject(mdc, ob);
    if (ok) {
        CLSID clsid;
        ok = png_clsid(clsid);
        if (ok) {
            Gdiplus::Bitmap gb(bmp, nullptr);
            ok = gb.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
        }
    }
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(hwnd, wdc);
    Gdiplus::GdiplusShutdown(tok);
    return ok;
}

// ---- the tray ----
void tray_add(Gui& s) {
    s.nid = NOTIFYICONDATAW{};
    s.nid.cbSize = sizeof s.nid;
    s.nid.hWnd = s.hwnd;
    s.nid.uID = 1;
    s.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s.nid.uCallbackMessage = WM_TRAY;
    s.nid.hIcon = s.icon;
    wcscpy_s(s.nid.szTip, L"caseclock");
    Shell_NotifyIconW(NIM_ADD, &s.nid);
}
void tray_update(Gui& s) {
    CaseRuntime& rt = shown(s);
    HICON fresh = make_icon(32, mood_color(rt.mood()));
    if (s.icon) DestroyIcon(s.icon);
    s.icon = fresh;
    s.nid.hIcon = s.icon;
    s.nid.uFlags = NIF_ICON | NIF_TIP;
    std::wstring tip = widen(rt.strip_text());
    if (tip.size() > 120) tip.resize(120);
    wcscpy_s(s.nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &s.nid);
    SendMessageW(s.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)s.icon);
}

// ---- the minute ----
void on_tick(Gui& s) {
    bool changed = false;
    for (size_t i = 0; i < s.cases->size(); ++i) {
        CaseRuntime& rt = *(*s.cases)[i];
        const int64_t m = case_minute(s, i);
        if (m == rt.now) continue;
        const size_t before = rt.tape.size();
        std::string err;
        rt.tick(m);
        if (rt.replaying && !rt.apply_scheduled(m, &err)) dbg("scheduled fact failed: %s", err.c_str());
        if (!rt.flush(&err)) set_status(s, L"could not write the tape: " + widen(err), 8);
        if (rt.tape.size() != before) {
            for (size_t k = before; k < rt.tape.size(); ++k) dbg("tick %lld %s", (long long)m, rt.tape[k].to_json().c_str());
        }
        changed = true;
    }
    if (changed) {
        tray_update(s);
        InvalidateRect(s.hwnd, nullptr, FALSE);
    }
}

void post_fact_from_driver(Gui& s, const std::string& event, int64_t minutes, const std::string& by) {
    CaseRuntime& rt = shown(s);
    std::string err;
    const int64_t at = case_minute(s, s.sel);
    if (!rt.add_fact(event, minutes, by.empty() ? "driver" : by, at, &err)) { dbg("driver fact failed: %s", err.c_str()); return; }
    dbg("driver fact %s %lld by %s at %lld", event.c_str(), (long long)minutes, by.c_str(), (long long)at);
    set_status(s, widen("fact from a driver: " + event + " at " + hhmm(minutes)));
    tray_update(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Gui& s = *g;
    switch (m) {
        case WM_CREATE: s.hwnd = h; s.edit_brush = CreateSolidBrush(kEditBg); return 0;
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, kText);
            SetBkColor(dc, kEditBg);
            return (LRESULT)s.edit_brush;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT c;
            GetClientRect(h, &c);
            const double t0 = now_seconds();
            HDC mdc = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
            HGDIOBJ ob = SelectObject(mdc, bmp);
            paint(s, mdc);
            BitBlt(dc, 0, 0, c.right, c.bottom, mdc, 0, 0, SRCCOPY);
            SelectObject(mdc, ob);
            DeleteObject(bmp);
            DeleteDC(mdc);
            EndPaint(h, &ps);
            s.paint_us = (now_seconds() - t0) * 1e6;
            return 0;
        }
        case WM_TIMER:
            if (wp == kTickTimer) on_tick(s);
            else if (wp == kShotTimer) {
                KillTimer(h, kShotTimer);
                RECT wr;
                GetWindowRect(h, &wr);
                PROCESS_MEMORY_COUNTERS pmc{};
                pmc.cb = sizeof pmc;
                GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
                dbg("measure dpi=%d window=%ldx%ld paint=%.0f us working_set=%llu KB peak=%llu KB", s.dpi, wr.right - wr.left, wr.bottom - wr.top, s.paint_us,
                    (unsigned long long)pmc.WorkingSetSize / 1024, (unsigned long long)pmc.PeakWorkingSetSize / 1024);
                const bool ok = save_shot(h, widen(s.opts.shot));
                fprintf(stderr, "caseclock: %s %s\n", ok ? "saved" : "could not save", s.opts.shot.c_str());
                dbg("shot %s %s", ok ? "saved" : "failed", s.opts.shot.c_str());
                DestroyWindow(h);
            } else if (wp == kStatusTimer) {
                KillTimer(h, kStatusTimer);
                s.status.clear();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0;
            dbg("key %u ctrl=%d", (unsigned)wp, ctrl ? 1 : 0);
            if (handle_key(s, wp, ctrl)) { InvalidateRect(h, nullptr, FALSE); return 0; }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (!s.opts.no_activate) SetFocus(h);
            if (PtInRect(&s.rcStrip, p) || PtInRect(&s.rcKeys, p)) {
                s.dragging = true;
                GetCursorPos(&s.drag_start);
                GetWindowRect(h, &s.drag_rc);
                SetCapture(h);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (s.dragging) {
                POINT p;
                GetCursorPos(&p);
                const int w = s.drag_rc.right - s.drag_rc.left, hh = s.drag_rc.bottom - s.drag_rc.top;
                SetWindowPos(h, nullptr, s.drag_rc.left + (p.x - s.drag_start.x), s.drag_rc.top + (p.y - s.drag_start.y), w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (s.dragging) {
                s.dragging = false;
                ReleaseCapture();
                RECT wr, wa;
                GetWindowRect(h, &wr);
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
                s.dock_top = (wr.top + wr.bottom) / 2 < (wa.top + wa.bottom) / 2;
                ini_save(s);
                place(s);
                dbg("dock %s", s.dock_top ? "top" : "bottom");
            }
            return 0;
        case WM_LBUTTONDBLCLK: open_explain_pane(s, [&] { Deadline d; return shown(s).nearest(d) ? d.event : std::string(); }()); return 0;
        case WM_MOUSEWHEEL:
            if (s.pane == Pane::Explain || s.pane == Pane::Signout) {
                s.scroll = std::max(0, s.scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_TRAY:
            if (lp == WM_LBUTTONUP) { if (s.hidden) from_tray(s); else to_tray(s); }
            else if (lp == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kMenuShow, s.hidden ? L"Show the strip" : L"Hide the strip");
                AppendMenuW(menu, MF_STRING | (s.topmost ? MF_CHECKED : 0), kMenuPin, L"Pinned on top\tCtrl+T");
                AppendMenuW(menu, MF_STRING | (s.show_keys ? MF_CHECKED : 0), kMenuKeys, L"Show the keys\tCtrl+K");
                AppendMenuW(menu, MF_STRING, kMenuSignout, L"Sign-out\tCtrl+S");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit caseclock");
                POINT p;
                GetCursorPos(&p);
                SetForegroundWindow(h);
                const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
                DestroyMenu(menu);
                if (cmd == kMenuShow) { if (s.hidden) from_tray(s); else to_tray(s); }
                else if (cmd == kMenuPin) toggle_topmost(s);
                else if (cmd == kMenuKeys) { s.show_keys = !s.show_keys; ini_save(s); place(s); }
                else if (cmd == kMenuSignout) { if (s.hidden) from_tray(s); open_signout_pane(s); }
                else if (cmd == kMenuQuit) DestroyWindow(h);
            }
            return 0;
        case WM_COPYDATA: {
            const COPYDATASTRUCT* cd = (const COPYDATASTRUCT*)lp;
            if (cd && cd->dwData == WM_APP_FACT && cd->lpData && cd->cbData) {
                const std::string payload((const char*)cd->lpData, cd->cbData);
                std::string event, minutes, by;
                size_t p = 0;
                while (p < payload.size()) {
                    size_t q = payload.find(';', p);
                    if (q == std::string::npos) q = payload.size();
                    const std::string kv = payload.substr(p, q - p);
                    p = q + 1;
                    const size_t eq = kv.find('=');
                    if (eq == std::string::npos) continue;
                    const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                    if (k == "event") event = v; else if (k == "minutes") minutes = v; else if (k == "by") by = v;
                }
                int64_t mins = 0;
                if (!event.empty() && parse_hhmm(minutes, mins)) { post_fact_from_driver(s, event, mins, by); return 1; }
                dbg("driver payload rejected: %s", payload.c_str());
            }
            return 0;
        }
        case WM_APP_FACT: {
            const std::vector<std::string> w = shown(s).watched();
            if ((size_t)lp < w.size()) post_fact_from_driver(s, w[(size_t)lp], (int64_t)(intptr_t)wp, "driver");
            return 0;
        }
        case WM_DPICHANGED: {
            s.dpi = HIWORD(wp);
            make_fonts(s);
            place(s);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: place(s); return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &s.nid);
            for (auto& c : *s.cases) { std::string err; c->flush(&err); }
            ini_save(s);
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProcW(h, m, wp, lp);
    }
}

}  // namespace

int run_gui(const Opts& o, std::vector<std::unique_ptr<CaseRuntime>>& cases) {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    using PFN_SetCtx = BOOL(WINAPI*)(HANDLE);
    if (auto p = (PFN_SetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext")) p((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */);
    else SetProcessDPIAware();

    static Gui state;
    g = &state;
    state.opts = o;
    state.cases = &cases;
    state.launch_seconds = now_seconds();
    for (auto& c : cases) state.launch_minute.push_back(c->now);
    state.ini = path_join(default_tape_dir(), "caseclock.ini");
    ini_load(state);
    if (o.no_keys) state.show_keys = false;
    if (!o.only.empty()) {
        for (size_t i = 0; i < cases.size(); ++i)
            if (cases[i]->doc.id == o.only) { state.sel = i; state.sel_pinned = true; }
    }
    dbg("start %zu cases, ini %s", cases.size(), state.ini.c_str());
    for (auto& c : cases) dbg("case %s now=%lld (%s) tape=%s rows=%zu", c->doc.id.c_str(), (long long)c->now, c->clock_source.c_str(), c->tape_path.c_str(), c->tape.size());

    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"caseclockStrip";
    state.icon = make_icon(32, kQuiet);
    wc.hIcon = state.icon;
    RegisterClassW(&wc);

    {
        using PFN_SysDpi = UINT(WINAPI*)();
        if (auto p = (PFN_SysDpi)GetProcAddress(u32, "GetDpiForSystem")) state.dpi = (int)p();
    }
    const DWORD ex = WS_EX_TOOLWINDOW | (state.topmost ? WS_EX_TOPMOST : 0) | (o.no_activate ? WS_EX_NOACTIVATE : 0);
    state.hwnd = CreateWindowExW(ex, wc.lpszClassName, L"caseclock", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    if (!state.hwnd) return 1;
    {
        using PFN_GetDpi = UINT(WINAPI*)(HWND);
        if (auto p = (PFN_GetDpi)GetProcAddress(u32, "GetDpiForWindow")) state.dpi = (int)p(state.hwnd);
    }
    make_fonts(state);
    place(state);
    tray_add(state);
    tray_update(state);
    ShowWindow(state.hwnd, o.no_activate ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!o.no_activate) SetFocus(state.hwnd);
    SetTimer(state.hwnd, kTickTimer, 1000, nullptr);
    if (!o.shot.empty()) SetTimer(state.hwnd, kShotTimer, 600, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) { cycle_case(state); InvalidateRect(state.hwnd, nullptr, FALSE); continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (state.edit_brush) DeleteObject(state.edit_brush);
    for (HFONT f : {state.fMain, state.fSmall, state.fMono})
        if (f) DeleteObject(f);
    if (state.icon) DestroyIcon(state.icon);
    return 0;
}

}  // namespace caseclock
