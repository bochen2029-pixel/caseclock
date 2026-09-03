// caseclock — the case clock: every deadline a donor case implies, derived from the facts already
// entered, counted down on a strip over the record, written to a tape with the chain that produced
// it. Console modes (--report --signout --explain --json --fact --replay --mcp --spool --about
// --selftest --export --verify) live here; the strip is strip.cpp. C/C++ only, OS APIs only, one
// static exe, no network stack (build.bat fails the build if one appears in the import table).
//
// Build: build.bat (MSVC, /std:c++20 /W4 /WX /permissive- /utf-8 /MT) -> caseclock.exe + caseclockw.exe
#include "app_util.h"
#include "casefile.h"
#include "clock.h"
#include "closure.h"
#include "hash.h"
#include "json.h"
#include "signout.h"
#include "sys.h"
#include "tape.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>

namespace caseclock {

int run_gui(const Opts& o, std::vector<std::unique_ptr<CaseRuntime>>& cases);   // strip.cpp

static const char* kDot = " \xC2\xB7 ";

// ======================================================================
// loading: packs, cases, tapes
// ======================================================================
struct Loaded {
    std::vector<RulePack> packs;
    std::vector<std::unique_ptr<CaseRuntime>> cases;
};

static bool case_now(const Opts& o, const CaseDoc& doc, int64_t& now, std::string& how) {
    if (o.has_now) { now = o.now; how = "--now"; return true; }
    if (!doc.reference_at.empty()) {
        LocalTime t;
        if (!parse_local_iso(doc.reference_at, t)) { how = "reference_at is not YYYY-MM-DDTHH:MM"; return false; }
        if (!minutes_since(t, now)) { how = "reference_at is not a valid local time"; return false; }
        how = "wall clock from reference_at " + doc.reference_at;
        return true;
    }
    if (doc.has_now) { now = doc.now; how = "the case file's now"; return true; }
    now = 0;
    how = "no clock in the case file: minute 0";
    return true;
}

static bool load_all(const Opts& o, Loaded& L, bool write, std::string* err) {
    for (const std::string& p : o.rules) {
        RulePack pk;
        if (!load_rule_pack(p, pk, err)) return false;
        L.packs.push_back(std::move(pk));
    }
    if (o.cases.empty()) { if (err) *err = "no case file given (caseclock CASE.json ...; --help)"; return false; }
    if (!o.tape.empty() && o.cases.size() > 1) { if (err) *err = "--tape names one file; give one case"; return false; }
    for (const std::string& path : o.cases) {
        CaseDoc doc;
        if (!load_case_file(path, doc, err)) return false;
        if (doc.id.empty()) doc.id = base_name(path);
        auto rt = std::make_unique<CaseRuntime>();
        int64_t now = 0;
        std::string how;
        if (!case_now(o, doc, now, how)) { if (err) *err = path + ": " + how; return false; }
        rt->by = o.by.empty() ? user_name() : o.by;
        rt->observe_only = !write;
        const std::string tp = !o.tape.empty() ? o.tape : tape_path_for(doc.id);
        if (!rt->load(doc, L.packs, tp, !o.clear, now, err)) return false;
        rt->clock_source = how;
        L.cases.push_back(std::move(rt));
    }
    return true;
}

static CaseRuntime* pick(const Opts& o, Loaded& L) {
    if (L.cases.empty()) return nullptr;
    if (!o.only.empty())
        for (auto& c : L.cases)
            if (c->doc.id == o.only) return c.get();
    return L.cases.front().get();
}

// ======================================================================
// --report: the reference's report(), byte for byte (packs first if given; the file's facts ignored,
// as the reference ignores them)
// ======================================================================
static int run_report(const Opts& o) {
    std::vector<RulePack> packs;
    std::string err;
    for (const std::string& p : o.rules) {
        RulePack pk;
        if (!load_rule_pack(p, pk, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
        packs.push_back(std::move(pk));
    }
    if (o.cases.empty()) { fprintf(stderr, "caseclock: --report needs a case file\n"); return 2; }
    int code = 0;
    for (const std::string& path : o.cases) {
        CaseDoc doc;
        if (!load_case_file(path, doc, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
        STN stn;
        if (!build_stn(packs, doc, {}, stn, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
        const Closure c = stn.close();
        std::string text;
        const bool has_now = o.has_now || doc.has_now;
        code = std::max(code, report_text(doc, stn, c, has_now, o.has_now ? o.now : doc.now, text));
        write_out(text);
    }
    return code;
}

// ======================================================================
// --json · --explain · --signout · --fact · --export · --verify
// ======================================================================
static int run_json(const Opts& o) {
    Loaded L;
    std::string err;
    if (!load_all(o, L, false, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    if (o.all) {
        Json a = Json::array();
        for (auto& c : L.cases) a.push(c->to_json());
        write_out(a.canonical() + "\n");
    } else {
        write_out(pick(o, L)->to_json().canonical() + "\n");
    }
    return 0;
}

static int run_explain(const Opts& o) {
    Loaded L;
    std::string err;
    if (!load_all(o, L, false, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    int found = 0;
    for (auto& c : L.cases) {
        if (!c->closure.has(o.explain_event)) continue;
        found++;
        if (L.cases.size() > 1) write_out("case: " + c->doc.id + "\n");
        write_out(c->explain(o.explain_event) + "\n");
    }
    if (!found) { fprintf(stderr, "caseclock: no loaded case has an event '%s'\n", o.explain_event.c_str()); return 1; }
    return 0;
}

static int run_signout(const Opts& o, bool record) {
    Loaded L;
    std::string err;
    if (!load_all(o, L, record, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    CaseRuntime* rt = pick(o, L);
    const std::string by = o.by.empty() ? user_name() : o.by;
    const std::string next = o.next.empty() ? "on-call" : o.next;
    const std::string text = render_signout(*rt, by, next);
    write_out(text);
    if (record && !rt->record_signout(text, by, next, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    return rt->infeasible() ? 1 : 0;
}

static int run_fact(const Opts& o) {
    Loaded L;
    std::string err;
    if (!load_all(o, L, true, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    CaseRuntime* rt = pick(o, L);
    int64_t minutes = 0;
    if (!parse_hhmm(o.fact_time, minutes)) { fprintf(stderr, "caseclock: --fact time '%s' is not HH:MM, HH:MM -1d, or minutes\n", o.fact_time.c_str()); return 2; }
    const size_t before = rt->tape.size();
    if (!rt->add_fact(o.fact_event, minutes, rt->by, rt->now, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    for (size_t i = before; i < rt->tape.size(); ++i) {
        const Entry& e = rt->tape[i];
        const Json& b = e.body;
        std::string t = b.str("text");
        if (e.kind == "fact") t = b.str("event") + " at " + hhmm(b.num("minutes")) + " (entered by " + b.str("by") + (b.get("supersedes") ? ", corrects an earlier entry)" : ")");
        else if (e.kind == "derived") t = b.str("event") + " by " + hhmm(b.num("latest")) + ssprintf(" \xC2\xB7 chain %zu", b.get("chain") ? b.get("chain")->arr.size() : 0);
        else if (e.kind == "withdrawn") t = b.str("event") + " was by " + hhmm(b.num("previous")) + (b.get("latest") && b.get("latest")->is_int() ? " \xE2\x86\x92 now by " + hhmm(b.get("latest")->i) : std::string(" \xE2\x86\x92 no longer bound"));
        else if (e.kind == "held") t = b.str("reason") + (b.get("event") ? ": " + b.str("event") + " by " + hhmm(b.num("latest")) : std::string());
        else if (e.kind == "infeasible") t = ssprintf("short by %lld min", (long long)b.num("short_by"));
        write_out(ssprintf("  %-10s %-14s %s\n", e.kind.c_str(), hhmm(e.at).c_str(), t.c_str()));
    }
    write_out(rt->strip_text() + "\n");
    return rt->infeasible() ? 1 : 0;
}

static bool tape_for(const Opts& o, std::string& path, std::string* err) {
    if (!o.tape.empty()) { path = o.tape; return true; }
    if (o.cases.empty()) { if (err) *err = "give a case file or --tape PATH"; return false; }
    CaseDoc doc;
    if (!load_case_file(o.cases.front(), doc, err)) return false;
    path = tape_path_for(doc.id.empty() ? base_name(o.cases.front()) : doc.id);
    return true;
}

static int run_export(const Opts& o) {
    std::string path, err;
    if (!tape_for(o, path, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    Tape t;
    bool enc = false;
    if (!Tape::load_file(path, t, &enc, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    const std::string text = t.to_text();
    if (o.export_path.empty() || o.export_path == "-") write_out(text);
    else if (!write_file(o.export_path, text, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    else fprintf(stderr, "caseclock: exported %zu rows of %s (%s) to %s in the clear\n", t.size(), t.case_id.c_str(), enc ? "DPAPI at rest" : "clear at rest", o.export_path.c_str());
    return 0;
}

static int run_verify(const Opts& o) {
    std::string path, err;
    if (!tape_for(o, path, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    std::string text;
    if (!read_file(path, text, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    Tape t;
    bool enc = false;
    const bool ok = Tape::load_file(path, t, &enc, &err);
    if (o.json) {
        Json j = Json::object();
        j.set("tape", Json::string(path));
        j.set("intact", Json::boolean(ok));
        j.set("rows", Json::integer((int64_t)t.size()));
        j.set("head", Json::string(t.head()));
        j.set("case", Json::string(t.case_id));
        j.set("encrypted", Json::boolean(enc));
        if (!ok) j.set("error", Json::string(err));
        write_out(j.canonical() + "\n");
    } else if (ok) {
        write_out(ssprintf("case %s \xE2\x80\x94 %zu entries, head %s\xE2\x80\xA6\nchain: INTACT (%s)\n", t.case_id.c_str(), t.size(), t.head().substr(0, 16).c_str(), enc ? "DPAPI at rest" : "clear"));
    } else {
        write_out("chain: BROKEN \xE2\x80\x94 " + err + "\n");
    }
    return ok ? 0 : 1;
}

// ======================================================================
// --replay: the case's facts in order at N× wall speed, on a scratch tape
// ======================================================================
static int run_replay(const Opts& o) {
    std::vector<RulePack> packs;
    std::string err;
    for (const std::string& p : o.rules) {
        RulePack pk;
        if (!load_rule_pack(p, pk, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
        packs.push_back(std::move(pk));
    }
    if (o.cases.empty()) { fprintf(stderr, "caseclock: --replay needs a case file\n"); return 2; }
    CaseDoc doc;
    if (!load_case_file(o.cases.front(), doc, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    if (doc.id.empty()) doc.id = base_name(o.cases.front());
    const ReplayPlan plan = plan_replay(doc, o.has_now, o.now);
    const int64_t start = plan.start, end = plan.end;
    const std::string scratch = !o.tape.empty() ? o.tape : path_join(path_join(temp_dir(), "caseclock-replay"), sanitize_file_name(doc.id) + ".tape.jsonl");
    make_dirs(dir_name(scratch));
    remove_file(scratch);
    CaseRuntime rt;
    rt.by = o.by.empty() ? "replay" : o.by;
    rt.replaying = true;
    rt.replay_start = start;
    rt.replay_end = end;
    rt.scheduled = plan.later;
    if (!rt.load(plan.at_start, packs, scratch, false, start, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
    write_out("replay " + doc.id + kDot + hhmm(start) + " \xE2\x86\x92 " + hhmm(end) + ssprintf(" at %d\xC3\x97", o.speed) + kDot + "tape " + scratch + "\n");
    if (doc.synthetic) write_out("SYNTHETIC" + std::string(kDot) + "no real donor data; every duration illustrative\n");
    size_t printed = 0;
    auto show_rows = [&]() {
        for (; printed < rt.tape.size(); ++printed) {
            const Entry& e = rt.tape[printed];
            if (e.kind == "said" || e.kind == "infeasible" || e.kind == "silence" || e.kind == "withdrawn" || e.kind == "fact") {
                std::string t = e.body.str("text");
                if (e.kind == "fact") t = e.body.str("event") + " at " + hhmm(e.body.num("minutes")) + " (entered by " + e.body.str("by") + ")";
                if (e.kind == "silence") t = ssprintf("hour %lld, %lld open, said nothing", (long long)e.body.num("hour"), (long long)e.body.num("open"));
                if (e.kind == "withdrawn") t = e.body.str("event") + " was by " + hhmm(e.body.num("previous"));
                if (e.kind == "infeasible") t = ssprintf("short by %lld min", (long long)e.body.num("short_by"));
                write_out("  " + pad_cp(hhmm(e.at), 13) + pad_cp(e.kind, 11) + t + "\n");
            }
        }
    };
    show_rows();
    for (int64_t m = start; m <= end; ++m) {
        if (m != start) rt.tick(m);
        if (!rt.apply_scheduled(m, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
        for (const ReplayLine& r : doc.replay)
            if (r.at == m) write_out("  " + pad_cp(hhmm(m), 13) + "\xE2\x94\x80\xE2\x94\x80 " + r.say + "\n");
        show_rows();
        if (o.speed > 0 && m < end) sleep_ms(60000 / o.speed);
    }
    write_out("\n" + rt.strip_text() + "\n\n");
    const std::string text = render_signout(rt, rt.by, "on-call");
    write_out(text);
    rt.record_signout(text, rt.by, "on-call", &err);
    return rt.infeasible() ? 1 : 0;
}

// ======================================================================
// --spool: lane "case", one line per said / infeasible row, for a fusor / TOWER tailer
// ======================================================================
static int run_spool(const Opts& o) {
    FILE* f = stdout;
    if (!o.spool_file.empty()) {
        f = fopen(o.spool_file.c_str(), "ab");
        if (!f) { fprintf(stderr, "caseclock: cannot open spool file: %s\n", o.spool_file.c_str()); return 1; }
    }
    std::map<std::string, size_t> seen;
    int frames = 0;
    bool first = true;
    for (;;) {
        Loaded L;
        std::string err;
        if (!load_all(o, L, false, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
        for (auto& c : L.cases) {
            size_t& from = seen[c->doc.id];
            if (first) {
                const std::string line = o.lane + "\t" + c->strip_text() + "\n";
                fwrite(line.data(), 1, line.size(), f);
                from = c->tape.size();
                continue;
            }
            for (; from < c->tape.size(); ++from) {
                const Entry& e = c->tape[from];
                if (e.kind != "said" && e.kind != "infeasible") continue;
                const std::string line = o.lane + "\t" + (e.kind == "said" ? e.body.str("text") : c->strip_text()) + "\n";
                fwrite(line.data(), 1, line.size(), f);
            }
        }
        fflush(f);
        first = false;
        if (o.frames > 0 && ++frames >= o.frames) break;
        sleep_ms(o.interval_ms);
    }
    if (f != stdout) fclose(f);
    return 0;
}

// ======================================================================
// --about: the organ describes itself as one JSON object (the contract `peek env` reads)
// ======================================================================
static int run_about() {
    const std::string exe = exe_path();
    Json j = Json::object();
    j.set("organ", Json::string("caseclock"));
    j.set("version", Json::string(kVersion));
    j.set("path", Json::string(exe));
    j.set("family", Json::string("opnaorta.ai tools \xC2\xB7 facet \xC2\xB7 vramtop \xC2\xB7 everywho \xC2\xB7 caseclock"));
    j.set("purpose", Json::string("the case clock: every deadline a donor case implies, derived from the facts already entered (a temporal closure in whole minutes, bit-identical to REGISTRAR floor/closure.py), on a 28-px strip over the record, written to an append-only hash-chained tape; the sign-out is its text form"));
    Json verbs = Json::array();
    auto verb = [&](const char* v, const char* what, const char* ex) {
        Json o = Json::object();
        o.set("verb", Json::string(v)); o.set("what", Json::string(what)); o.set("example", Json::string(ex));
        verbs.push(o);
    };
    verb("caseclock CASE.json", "the strip, pinned over the record (caseclockw.exe = no console)", "caseclockw.exe floor/cases/tr-4118.synthetic.json");
    verb("caseclock --json CASE.json", "every open deadline with its chain, slack and the clock, one JSON object (--all: every case)", "caseclock --json floor/cases/tr-4118.synthetic.json");
    verb("caseclock --signout CASE.json", "the sign-out, now, as text; records a signout row", "caseclock --signout --by \"J. Alvarez\" CASE.json");
    verb("caseclock --explain EVENT CASE.json", "the chain of constraints behind a deadline (the reference's explain text)", "caseclock --explain serology_drawn CASE.json");
    verb("caseclock --fact EVENT TIME CASE.json", "a human enters a time at the terminal; the closure reruns; rows are written", "caseclock --fact serology_drawn 22:10 CASE.json");
    verb("caseclock --report CASE.json", "REGISTRAR's report text, byte for byte", "caseclock --report CASE.json");
    verb("caseclock --replay CASE.json", "a synthetic case at 60x on a scratch tape (the tools-page capture)", "caseclock --replay --speed 0 floor/cases/tr-4118.synthetic.json");
    verb("caseclock --export FILE CASE.json", "the tape in the clear for the auditor (- = stdout)", "caseclock --export - CASE.json");
    verb("caseclock --verify CASE.json", "walk the tape's hash chain", "caseclock --verify CASE.json");
    verb("caseclock --spool CASE.json", "lane<TAB>text stream of every line the strip spoke (lane case)", "caseclock --spool CASE.json");
    verb("caseclock --selftest", "closure parity with the reference on every fixture, tape bytes, chain, sign-out determinism, the speaking rules, DPAPI, the import table", "caseclock --selftest");
    j.set("verbs", verbs);
    Json mcp = Json::object();
    mcp.set("command", Json::string(exe));
    Json args = Json::array();
    args.push(Json::string("--mcp"));
    mcp.set("args", args);
    Json tools = Json::array();
    tools.push(Json::string("case_clock")); tools.push(Json::string("case_explain")); tools.push(Json::string("case_signout"));
    mcp.set("tools", tools);
    mcp.set("register", Json::string("claude mcp add caseclock -- " + exe + " --mcp CASE.json"));
    mcp.set("writes", Json::string("nothing: no tool enters a fact; a fact is a human's keystroke"));
    j.set("mcp", mcp);
    const std::vector<std::string> dlls = imported_dlls();
    bool net = false;
    for (const std::string& d : dlls)
        for (const char* bad : {"ws2_32", "wininet", "winhttp", "urlmon", "dnsapi", "iphlpapi", "wsock32"})
            if (d.find(bad) != std::string::npos) net = true;
    Json health = Json::object();
    health.set("ok", Json::boolean(!net));
    health.set("tape_dir", Json::string(default_tape_dir()));
    health.set("tapes", Json::integer((int64_t)list_files(default_tape_dir(), ".tape.jsonl").size()));
    health.set("detail", Json::string(net ? "a network DLL is in the import table: this build does not ship" : "no network DLL in the import table; nothing leaves the building"));
    j.set("health", health);
    j.set("imports", Json::strings(dlls));
    j.set("network_stack", Json::boolean(false));
    j.set("model", Json::boolean(false));
    j.set("phi", Json::string("none in the repository, ever; the tape is DPAPI-encrypted per user at rest; the sign-out leaves on paper"));
    j.set("docs", Json::string(path_join(exe_dir(), "README.md")));
    Json tape = Json::object();
    tape.set("writes", Json::string("%LOCALAPPDATA%\\caseclock\\<case>.tape.jsonl (REGISTRAR core/tape.py format, DPAPI at rest; --export for the clear)"));
    tape.set("reads", Json::string("REGISTRAR case JSON (floor/cases/*.json); rule packs (--rules)"));
    j.set("tape", tape);
    write_out(j.canonical() + "\n");
    return net ? 2 : 0;
}

// ======================================================================
// --mcp: newline-delimited JSON-RPC 2.0 over stdio, read-only, three tools
// ======================================================================
static std::string jv_id(const Json& v) {
    if (v.is_str()) return json_quote(v.s);
    if (v.is_int()) return std::to_string(v.i);
    if (v.type == Json::Number) return v.s;
    return "null";
}
static void mcp_send(const std::string& body) { write_out(body + "\n"); }
static void mcp_result(const std::string& id, const std::string& result) { mcp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}"); }
static void mcp_error(const std::string& id, int code, const std::string& msg) {
    mcp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ssprintf(",\"error\":{\"code\":%d,\"message\":", code) + json_quote(msg) + "}}");
}
static std::string mcp_text(const std::string& text, bool is_error = false) {
    return "{\"content\":[{\"type\":\"text\",\"text\":" + json_quote(text) + "}]" + (is_error ? ",\"isError\":true}" : "}");
}
static const char* kToolsList =
    "{\"tools\":[{"
    "\"name\":\"case_clock\","
    "\"description\":\"The case clock for the loaded donor case(s): every open deadline with its latest minute, slack, and the chain of constraints that produced it, plus the facts entered, the rule set and the tape digest. Read-only; the same answer the strip shows.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"case\":{\"type\":\"string\",\"description\":\"case id (default: every loaded case)\"}}}},{"
    "\"name\":\"case_explain\","
    "\"description\":\"The chain of constraints behind one event's deadline: the shortest path that realises the bound, one constraint per line with its layer and cumulative time. This is the derivation, not commentary.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"event\":{\"type\":\"string\"},\"case\":{\"type\":\"string\"}},\"required\":[\"event\"]}},{"
    "\"name\":\"case_signout\","
    "\"description\":\"The sign-out as text, rendered from the live tape at this instant (due in order, what happened since the last sign-out, the chain). Read-only: an agent rendering it does not record a signout row; a human printing it does.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"case\":{\"type\":\"string\"},\"by\":{\"type\":\"string\"},\"next\":{\"type\":\"string\"}}}}]}";

static int run_mcp(const Opts& base) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Json v;
        if (!json_parse(line, v) || !v.is_obj()) { mcp_error("null", -32700, "parse error"); continue; }
        const std::string method = v.str("method");
        const Json* idv = v.get("id");
        if (!idv || idv->is_null()) continue;   // notification
        const std::string id = jv_id(*idv);
        if (method == "initialize") {
            std::string proto = "2025-06-18";
            if (const Json* p = v.get("params")) proto = p->str("protocolVersion", proto);
            mcp_result(id, "{\"protocolVersion\":" + json_quote(proto) + ",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"caseclock\",\"version\":" + json_quote(kVersion) + "}}");
        } else if (method == "ping") {
            mcp_result(id, "{}");
        } else if (method == "tools/list") {
            mcp_result(id, kToolsList);
        } else if (method == "tools/call") {
            const Json* p = v.get("params");
            const std::string tool = p ? p->str("name") : "";
            const Json* a = p ? p->get("arguments") : nullptr;
            auto arg = [&](const char* k) { return a ? a->str(k) : std::string(); };
            Loaded L;
            std::string err;
            if (!load_all(base, L, false, &err)) { mcp_result(id, mcp_text(err, true)); continue; }
            const std::string want = arg("case");
            std::vector<CaseRuntime*> sel;
            for (auto& c : L.cases)
                if (want.empty() || c->doc.id == want) sel.push_back(c.get());
            if (sel.empty()) { mcp_result(id, mcp_text("no loaded case is '" + want + "'", true)); continue; }
            if (tool == "case_clock") {
                Json out = Json::array();
                for (CaseRuntime* c : sel) out.push(c->to_json());
                mcp_result(id, mcp_text(sel.size() == 1 ? out.arr[0].canonical() : out.canonical()));
            } else if (tool == "case_explain") {
                const std::string ev = arg("event");
                std::string text;
                for (CaseRuntime* c : sel)
                    if (c->closure.has(ev)) text += (sel.size() > 1 ? "case: " + c->doc.id + "\n" : "") + c->explain(ev) + "\ntape " + c->tape.head() + "\n";
                if (text.empty()) mcp_result(id, mcp_text("no loaded case has an event '" + ev + "'", true));
                else mcp_result(id, mcp_text(text));
            } else if (tool == "case_signout") {
                std::string text;
                for (CaseRuntime* c : sel) text += render_signout(*c, arg("by").empty() ? "agent" : arg("by"), arg("next").empty() ? "on-call" : arg("next")) + "\n";
                mcp_result(id, mcp_text(text));
            } else {
                mcp_error(id, -32602, "unknown tool: " + tool);
            }
        } else {
            mcp_error(id, -32601, "method not found: " + method);
        }
    }
    return 0;
}

// ======================================================================
// --selftest
// ======================================================================
static std::string find_root(const Opts& o) {
    if (!o.root.empty()) return o.root;
    std::string d = exe_dir();
    for (int up = 0; up < 5; ++up) {
        if (file_exists(path_join(d, "floor\\cases\\README.md"))) return d;
        const std::string parent = dir_name(d.substr(0, d.size() - 1));
        if (parent.empty() || parent == d) break;
        d = parent;
    }
    return current_dir();
}

struct Check {
    int fails = 0, passes = 0;
    void operator()(bool ok, const std::string& what) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", what.c_str());
        if (ok) passes++; else fails++;
    }
};

static bool read_expected(const std::string& root, const std::string& name, Json& out, std::string& err) {
    std::string text;
    const std::string p = path_join(root, "tests\\expected\\" + name);
    if (!read_file(p, text, &err)) return false;
    return json_parse(text, out, &err);
}

static void selftest_fixture(Check& check, const std::string& root, const std::string& file) {
    const std::string name = file.substr(0, file.size() - 5);
    printf("fixture %s\n", file.c_str());
    CaseDoc doc;
    std::string err;
    if (!load_case_file(path_join(root, "floor\\cases\\" + file), doc, &err)) { check(false, "load: " + err); return; }
    check(doc.synthetic, "declares itself synthetic");
    check(starts_with(doc.note, "SYNTHETIC"), "the note's first word is SYNTHETIC");
    Json ex;
    if (!read_expected(root, name + ".closure.json", ex, err)) { check(false, "expected: " + err); return; }
    STN stn;
    if (!build_stn({}, doc, {}, stn, &err)) { check(false, "build: " + err); return; }
    const Closure c = stn.close();
    // names, constraints
    check(Json::strings(stn.names) == *ex.get("names"), "event order (names)");
    {
        bool ok = ex.get("constraints")->arr.size() == stn.constraints.size();
        for (size_t i = 0; ok && i < stn.constraints.size(); ++i) ok = stn.constraints[i].render() == ex.get("constraints")->arr[i].str("render");
        check(ok, ssprintf("constraints render as the reference (%zu)", stn.constraints.size()));
    }
    // D, nxt
    {
        const Json& D = *ex.get("D");
        bool ok = (int)D.arr.size() == c.n;
        for (int i = 0; ok && i < c.n; ++i)
            for (int j = 0; ok && j < c.n; ++j) ok = D.arr[(size_t)i].arr[(size_t)j].is_int() && D.arr[(size_t)i].arr[(size_t)j].i == c.d(i, j);
        check(ok, ssprintf("D == reference D (%dx%d, every entry)", c.n, c.n));
        const Json& N = *ex.get("nxt");
        ok = (int)N.arr.size() == c.n;
        for (int i = 0; ok && i < c.n; ++i)
            for (int j = 0; ok && j < c.n; ++j) ok = N.arr[(size_t)i].arr[(size_t)j].i == c.next(i, j);
        check(ok, "nxt == reference nxt (every entry)");
        const Json& O = *ex.get("origin");
        ok = O.arr.size() == c.origin.size();
        for (const Json& t : O.arr) {
            if (!ok) break;
            auto it = c.origin.find({(int)t.arr[0].i, (int)t.arr[1].i});
            ok = it != c.origin.end() && (int64_t)it->second == t.arr[2].i;
        }
        check(ok, "origin (the tightest constraint per edge, first of equals)");
    }
    check(c.consistent() == ex.flag("consistent"), std::string("consistent == ") + (ex.flag("consistent") ? "true" : "false"));
    // windows, chains, explain
    {
        bool okw = true, okp = true, oke = true;
        for (const std::string& n : stn.names) {
            const Json* w = ex.get("windows")->get(n);
            okw = okw && w && w->arr[0].i == c.earliest(n) && w->arr[1].i == c.latest(n);
            std::vector<std::string> bp;
            for (const Constraint& k : c.binding_path(n)) bp.push_back(k.render());
            okp = okp && *ex.get("binding_path")->get(n) == Json::strings(bp);
            oke = oke && ex.get("explain")->str(n) == c.explain(n);
        }
        check(okw, "earliest/latest of every event");
        check(okp, "binding_path of every event");
        check(oke, "explain() text of every event, byte for byte");
    }
    {
        const std::vector<std::string> cyc = c.negative_cycle();
        const Json* ec = ex.get("negative_cycle");
        check((cyc.empty() && ec->is_null()) || (!cyc.empty() && *ec == Json::strings(cyc)), "negative_cycle as the reference walks it");
        std::vector<std::string> cc;
        int64_t total = 0;
        for (const Constraint& k : c.cycle_constraints(cyc)) { cc.push_back(k.render()); total += k.weight; }
        check(*ex.get("cycle_constraints") == Json::strings(cc), "cycle_constraints");
        if (!cyc.empty()) check(-total == ex.num("short_by"), ssprintf("short by %lld", (long long)-total));
    }
    {
        std::string text;
        const int code = report_text(doc, stn, c, doc.has_now, doc.now, text);
        const bool same = text == ex.str("report");
        check(same, "report() text, byte for byte");
        if (!same) {
            size_t i = 0;
            const std::string& want = ex.str("report");
            while (i < text.size() && i < want.size() && text[i] == want[i]) ++i;
            printf("       first difference at byte %zu\n       got:  %s\n       want: %s\n", i, text.substr(i, 60).c_str(), want.substr(i, 60).c_str());
        }
        check(code == (int)ex.num("report_exit"), "report() exit code");
    }
    if (const Json* sl = ex.get("slack_at_now")) {
        bool ok = true;
        for (const auto& kv : sl->obj) ok = ok && c.slack(kv.first, doc.now) == kv.second.i;
        check(ok, "slack at the file's now");
    }
    {
        bool ok = true;
        for (const auto& kv : ex.get("hhmm")->obj) ok = ok && hhmm(strtoll(kv.first.c_str(), nullptr, 10)) == kv.second.s;
        check(ok, "hhmm() samples (day offsets, unbounded)");
    }
    // the fixture's own expected block
    if (doc.expected.is_obj()) {
        const Json& e = doc.expected;
        check(c.consistent() == e.flag("consistent"), "fixture.expected.consistent");
        if (const Json* l = e.get("latest")) { bool ok = true; for (const auto& kv : l->obj) ok = ok && c.latest(kv.first) == kv.second.i; check(ok, "fixture.expected.latest"); }
        if (const Json* l = e.get("earliest")) { bool ok = true; for (const auto& kv : l->obj) ok = ok && c.earliest(kv.first) == kv.second.i; check(ok, "fixture.expected.earliest"); }
        if (const Json* l = e.get("chain_hops")) { bool ok = true; for (const auto& kv : l->obj) ok = ok && (int64_t)c.binding_path(kv.first).size() == kv.second.i; check(ok, "fixture.expected.chain_hops"); }
        if (const Json* l = e.get("slack_at_now")) { bool ok = true; for (const auto& kv : l->obj) ok = ok && c.slack(kv.first, doc.now) == kv.second.i; check(ok, "fixture.expected.slack_at_now"); }
        if (const Json* l = e.get("cycle")) check(*l == Json::strings(c.negative_cycle()), "fixture.expected.cycle");
        if (const Json* l = e.get("short_by")) { int64_t t = 0; for (const Constraint& k : c.cycle_constraints(c.negative_cycle())) t += k.weight; check(-t == l->i, "fixture.expected.short_by"); }
    }
}

static int run_selftest(const Opts& o) {
    Check check;
    const std::string root = find_root(o);
    printf("caseclock %s --selftest\nroot %s\n", kVersion, root.c_str());
    std::string err;

    // ---- hashes
    {
        Json vec;
        if (read_expected(root, "hash-vectors.json", vec, err)) {
            bool ok = true;
            int n = 0;
            for (const auto& kv : vec.obj) {
                std::string in = kv.first;
                if (kv.first == "sha256:abc") { ok = ok && sha256_hex("abc") == kv.second.s; continue; }
                if (starts_with(kv.first, "a*")) in = std::string((size_t)strtol(kv.first.c_str() + 2, nullptr, 10), 'a');
                ok = ok && blake2b256_hex(in) == kv.second.s;
                n++;
            }
            check(ok, ssprintf("BLAKE2b-256 == hashlib.blake2b(digest_size=32) on %d vectors (block boundaries included)", n));
            check(sha256_hex("abc") == vec.str("sha256:abc"), "SHA-256 (bcrypt) == hashlib.sha256");
        } else check(false, "hash vectors: " + err);
    }

    // ---- canonical JSON and the tape, against the reference's bytes
    {
        std::string ref, script_text;
        Json script;
        if (read_file(path_join(root, "tests\\expected\\reference-tape.jsonl"), ref, &err) && read_expected(root, "tape-script.json", script, err)) {
            Tape t(script.str("case_id"));
            for (const Json& r : script.get("rows")->arr) t.append(r.arr[0].s, r.arr[1].i, r.arr[2]);
            check(t.to_text() == ref, ssprintf("tape written here == tape written by core/tape.py, byte for byte (%zu rows: unicode, escapes, nesting)", t.size()));
            check(t.verify(&err), "the chain verifies: " + (err.empty() ? std::string("intact") : err));
            Tape back;
            check(Tape::from_text(ref, back, &err) && back.to_text() == ref, "the reference's file parses, verifies and re-serialises to the same bytes");
            // a flipped byte
            std::string bad = ref;
            const size_t at = bad.find("\"minutes\":-600");
            if (at != std::string::npos) bad[at + 12] = '7';
            Tape broken;
            check(!Tape::from_text(bad, broken, &err) && err.find("altered") != std::string::npos, "a flipped byte in a body is detected: " + err);
            // two rows swapped
            std::vector<std::string> lines;
            size_t p = 0;
            while (p < ref.size()) { size_t q = ref.find('\n', p); lines.push_back(ref.substr(p, q - p)); p = q + 1; }
            std::swap(lines[3], lines[4]);
            std::string sw;
            for (const std::string& l : lines) sw += l + "\n";
            check(!Tape::from_text(sw, broken, &err), "two rows swapped are detected: " + err);
            // supersedes
            check(t.superseded().count(9) == 1 && t.current().size() == t.size() - 1, "a correction supersedes; the superseded row stays on the tape");
        } else check(false, "reference tape: " + err);
    }

    // ---- every fixture
    for (const std::string& f : list_files(path_join(root, "floor\\cases"), ".json")) selftest_fixture(check, root, f);

    // ---- a 60-event case: closure time
    {
        STN s;
        for (int i = 1; i <= 60; ++i) {
            const std::string a = ssprintf("e%02d", i), b = ssprintf("e%02d", i - 1);
            if (i > 1) s.at_least(a, b, 10 + (i % 7), "step", "L3");
            if (i % 5 == 0) s.at_most(a, REFERENCE, 20 * i + 100, "window", "L2");
        }
        s.window("e60", 600, 1400, "or", "L2");
        Closure c = s.close();
        const double t0 = now_seconds();
        for (int k = 0; k < 200; ++k) c = s.close();
        const double us = (now_seconds() - t0) / 200.0 * 1e6;
        check(c.consistent() && c.n == 61, ssprintf("60-event case closes in %.0f us (200 closures averaged)", us));
    }

    // ---- the speaking rules on a scripted clock (in memory, no tape file)
    {
        printf("the speaking rules\n");
        CaseDoc doc;
        if (load_case_file(path_join(root, "floor\\cases\\tr-4118.synthetic.json"), doc, &err)) {
            CaseRuntime rt;
            rt.by = "selftest";
            bool ok = rt.load(doc, {}, "", false, -305, &err);   // 18:55 (-1d), just before the shift's first hour mark
            check(ok, "load at 18:55 (-1d): " + (ok ? std::string("ok") : err));
            auto kinds = [&](const std::string& kind, const std::string& reason = "") {
                int n = 0;
                for (const Entry& e : rt.tape.entries())
                    if (e.kind == kind && (reason.empty() || e.body.str("reason") == reason)) n++;
                return n;
            };
            check(kinds("note") == 1 && kinds("rules") == 1 && kinds("fact") == 1, "load writes note, rules, and the file's fact");
            check(kinds("derived") == 7, ssprintf("load derives every watched event with a finite latest (%d of 7)", kinds("derived")));
            check(kinds("held", "lead_not_reached") == 1 && kinds("said") == 0, "195 min of slack: nothing said, one held (lead_not_reached)");
            Deadline d;
            check(rt.nearest(d) && d.event == "serology_drawn" && d.latest == -105 && d.slack == 200, "nearest: serology_drawn by 22:15 (-1d), 200 min");
            for (int64_t m = -304; m <= -20; ++m) rt.tick(m);
            auto said_at = [&](const std::string& reason) {
                for (const Entry& e : rt.tape.entries())
                    if (e.kind == "said" && e.body.str("reason") == reason) return e.at;
                return (int64_t)INF;
            };
            check(said_at("lead_60") == -165, "lead_60 said at 21:15 (-1d), 60 min before");
            check(said_at("lead_15") == -120, "lead_15 said at 22:00 (-1d)");
            check(said_at("due") == -105, "due said at 22:15 (-1d)");
            check(said_at("breach") == -104, "breach said at 22:16 (-1d), the first minute past");
            check(kinds("said") == 4, ssprintf("exactly four lines spoken over the shift (%d)", kinds("said")));
            check(kinds("silence") == 3 && rt.state.silence_hours.count(-5) && rt.state.silence_hours.count(-4) && rt.state.silence_hours.count(-3),
                  "silence rows at 19:00, 20:00 and 21:00 only (22:00 and 23:00 had something due)");
            check(rt.strip_text() == "TR-4118 \xC2\xB7 serology_drawn by 22:15 (-1d) \xC2\xB7 BREACHED 85 min \xC2\xB7 chain 7", "the strip line at 23:40: " + rt.strip_text());
            check(rt.mood() == CaseRuntime::Mood::Red, "mood red");
            // a fact that makes the plan infeasible: drawn at 22:20 leaves the lab 355 min, it needs 360
            const size_t before = rt.tape.size();
            ok = rt.add_fact("serology_drawn", -100, "selftest", -20, &err);
            check(ok && rt.infeasible(), "fact serology_drawn at 22:20 (-1d): the plan is infeasible (the lab needs 360 min, 355 remain)");
            check(kinds("infeasible") == 1 && kinds("said", "infeasible") == 1, "infeasible row + said infeasible, immediately");
            check(kinds("withdrawn") >= 7, ssprintf("every derived bound withdrawn (%d)", kinds("withdrawn")));
            check(rt.tape.size() > before && rt.mood() == CaseRuntime::Mood::Violet, "mood violet; the strip: " + rt.strip_text());
            // the correction: drawn at 22:10 (an earlier entry was wrong) — feasible again
            ok = rt.add_fact("serology_drawn", -110, "selftest", -19, &err);
            check(ok && !rt.infeasible(), "corrected fact serology_drawn at 22:10 (-1d): feasible again");
            check(rt.tape.entries().back().kind != "fact" && rt.state.facts.size() == 3 && rt.tape.superseded().size() == 1, "the correction supersedes the earlier fact; both stay on the tape");
            check(kinds("note") == 2, "a note says the plan is feasible again");
            check(rt.nearest(d) && d.latest == 255 && (d.event == "match_run" || d.event == "serology_resulted"), "nearest is now 04:15 (match_run and serology_resulted tie; the name breaks it): " + rt.strip_text());
            // rate: two lines in one minute
            CaseRuntime r2;
            CaseDoc two;
            two.id = "TWO";
            two.synthetic = true;
            two.note = "SYNTHETIC. two deadlines at the same minute";
            for (const char* ev : {"a", "b"}) {
                ConstraintSpec cs;
                cs.kind = "window"; cs.event = ev; cs.opens = 0; cs.closes = 100; cs.label = ev; cs.layer = "L2";
                two.constraints.push_back(cs);
            }
            ok = r2.load(two, {}, "", false, 0, &err);
            for (int64_t m = 1; m <= 40; ++m) r2.tick(m);
            auto count2 = [&](const std::string& kind, const std::string& reason) { int n = 0; for (const Entry& e : r2.tape.entries()) if (e.kind == kind && e.body.str("reason") == reason) n++; return n; };
            check(ok && count2("said", "lead_60") == 1 && count2("held", "next_due") == 1, "two deadlines cross lead_60 in the same minute: one said, one held (next_due)");
            r2.tick(41);
            check(count2("said", "lead_60") == 2, "the held one is said the next minute");
            // rate: a fact in the minute a line was said
            ConstraintSpec cs;
            cs.kind = "window"; cs.event = "c"; cs.opens = 0; cs.closes = 90; cs.label = "c"; cs.layer = "L2";
            r2.doc.constraints.push_back(cs);
            r2.doc.watch.clear();
            ok = r2.add_fact("d", 5, "selftest", 41, &err);   // a fact reruns the closure; c appears within its lead
            check(ok && count2("held", "rate") == 1, "a new deadline within the lead, in a minute that already spoke: held (rate)");
            r2.tick(42);
            check(count2("held", "rate") == 1 && count2("said", "lead_60") == 3, "said the next minute");
        } else check(false, "load tr-4118: " + err);
    }

    // ---- sign-out determinism: two folds over one tape agree byte for byte
    {
        CaseDoc doc;
        if (load_case_file(path_join(root, "floor\\cases\\tr-4118.synthetic.json"), doc, &err)) {
            CaseRuntime a;
            a.by = "selftest";
            a.load(doc, {}, "", false, -300, &err);
            for (int64_t m = -299; m <= -20; ++m) a.tick(m);
            const std::string s1 = render_signout(a, "J. Alvarez (synthetic)", "on-call");
            const std::string s2 = render_signout(a, "J. Alvarez (synthetic)", "on-call");
            check(s1 == s2, "the same runtime renders the same sign-out twice");
            const std::string text = a.tape.to_text();
            CaseRuntime b;
            b.by = "someone else";
            Tape t;
            bool ok = Tape::from_text(text, t, &err);
            b.observe_only = true;
            b.has_preload = true;
            b.preload = t;
            ok = ok && b.load(doc, {}, "", false, -20, &err);
            const std::string s3 = render_signout(b, "J. Alvarez (synthetic)", "on-call");
            check(ok && s3 == s1 && b.tape.size() == a.tape.size(), ssprintf("a second fold over the same tape renders the same bytes (%zu rows, %zu bytes)", a.tape.size(), s1.size()));
            check(s1.find("DUE, IN ORDER") != std::string::npos && s1.find("BREACHED 85 min") != std::string::npos && s1.find("SYNTHETIC CASE") != std::string::npos, "the sign-out carries the breach, the order, the synthetic banner");
            if (!o.quiet) { printf("---- sign-out ----\n%s----\n", s1.c_str()); }
        }
    }

    // ---- DPAPI round trip and the encrypted tape file
    {
        std::string b64, back;
        const std::string msg = "SYNTHETIC \xC2\xB7 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80 row";
        const bool ok = dpapi_protect(msg, b64, &err) && dpapi_unprotect(b64, back, &err) && back == msg;
        check(ok, "DPAPI protect/unprotect round trip: " + (ok ? std::string("ok") : err));
        Tape t("SYNTHETIC-DPAPI");
        t.append("note", 0, Json::object().set("text", Json::string("SYNTHETIC")));
        t.append("fact", 5, Json::object().set("event", Json::string("x")).set("minutes", Json::integer(5)));
        const std::string p = path_join(temp_dir(), "caseclock-selftest.tape.jsonl");
        remove_file(p);
        bool ok2 = t.save_file(p, true, &err);
        Tape t2("SYNTHETIC-DPAPI");
        t2.append("note", 0, Json::object().set("text", Json::string("SYNTHETIC")));
        t2.append("fact", 5, Json::object().set("event", Json::string("x")).set("minutes", Json::integer(5)));
        t2.append("said", 6, Json::object().set("text", Json::string("hello")));
        ok2 = ok2 && t2.append_file(p, true, 2, &err);
        std::string raw;
        read_file(p, raw);
        Tape got;
        bool enc = false;
        ok2 = ok2 && Tape::load_file(p, got, &enc, &err);
        check(ok2 && enc && got.to_text() == t2.to_text() && raw.find("SYNTHETIC") == std::string::npos, "an encrypted tape: written, appended, read back equal; no clear text on disk");
        remove_file(p);
    }

    // ---- the import table
    {
        const std::vector<std::string> dlls = imported_dlls();
        bool net = false;
        std::string list;
        for (const std::string& d : dlls) {
            list += (list.empty() ? "" : " ") + d;
            for (const char* bad : {"ws2_32", "wininet", "winhttp", "urlmon", "dnsapi", "iphlpapi", "wsock32"})
                if (d.find(bad) != std::string::npos) net = true;
        }
        check(!dlls.empty() && !net, "no network DLL in the import table: " + list);
    }

    // ---- parsing
    {
        int64_t v = 0;
        check(parse_hhmm("22:15 (-1d)", v) && v == -105 && parse_hhmm("22:15 -1d", v) && v == -105 && parse_hhmm("04:15", v) && v == 255 &&
                  parse_hhmm("-20", v) && v == -20 && parse_hhmm("00:00 +2d", v) && v == 2880 && !parse_hhmm("25:00", v) && !parse_hhmm("abc", v),
              "parse_hhmm: HH:MM, day offsets, minutes; rejects 25:00");
        check(floor_div(-1, 60) == -1 && floor_mod(-1, 60) == 59 && floor_div(-120, 60) == -2 && floor_mod(-120, 60) == 0, "floor division for negative minutes");
        Json j;
        check(json_parse("{\"a\":[1,-2,\"x\\u00e9\\ud83d\\ude00\",true,null,{}],\"b\":1.5e3}", j) && j.get("a")->arr[2].s == "x\xC3\xA9\xF0\x9F\x98\x80" && j.get("b")->type == Json::Number,
              "JSON: escapes, surrogate pairs, ints vs numbers");
        check(!json_parse("{\"a\":1,}", j) && !json_parse("[1] x", j) && !json_parse("{\"a\":\"\x01\"}", j), "JSON: strict (trailing comma, trailing text, raw control char rejected)");
    }

    printf("\n%d passed, %d failed\n", check.passes, check.fails);
    return check.fails ? 1 : 0;
}

// ======================================================================
// help, args, main
// ======================================================================
static const char* kHelp = R"HELP(caseclock %s - the case clock: every deadline the case implies, on a strip over the record

  caseclock CASE.json [...]        load case(s); the strip appears, pinned (caseclockw.exe: no console)
  caseclock --json CASE.json       every open deadline with its chain, slack and the clock (--all: every case)
  caseclock --signout CASE.json    the sign-out, now, as text; records a signout row (--by, --next)
  caseclock --explain EVENT CASE   the chain of constraints behind a deadline
  caseclock --fact EVENT TIME CASE enter a time at the terminal (TIME: 22:15, "22:15 -1d", or minutes)
  caseclock --report CASE.json     REGISTRAR's report text, byte for byte
  caseclock --replay CASE.json     a synthetic case at --speed N x wall speed on a scratch tape (0 = instant)
  caseclock --export FILE CASE     the tape in the clear (FILE or -), for the auditor
  caseclock --verify CASE          walk the tape's hash chain
  caseclock --spool CASE.json      lane<TAB>text stream of every line spoken (fusor / TOWER tailer food)
  caseclock --mcp CASE.json        read-only MCP over stdio: case_clock, case_explain, case_signout
  caseclock --about                the organ's self-description, imports included, as JSON
  caseclock --selftest             parity with the reference on every fixture, tape bytes, the rules, DPAPI, imports

options
  --rules PACK.json      a rule pack (repeatable, applied before the case; L0/L1 ship once they carry citations)
  --now T                the case clock's minute (minutes from the reference, or HH:MM [+/-Nd])
  --tape PATH            the tape file (default %%LOCALAPPDATA%%\caseclock\<id>.tape.jsonl, DPAPI at rest)
  --clear                write the tape in the clear (replays, tests, an auditor's copy)
  --by NAME              who is at the keyboard (facts, the sign-out header); default: the Windows user
  --next NAME            who takes over (the sign-out header)
  --only CASE            pin the strip / --json to one case id
  --all                  --json: every loaded case
  --speed N              --replay: N x wall speed (default 60; 0 = no waiting)
  --frames N             --spool: stop after N polls;  --interval MS (default 2000);  --spool-file P;  --lane L
  --shot FILE.png        the strip: render once, save, exit;  --no-activate: never take the keyboard
  --no-keys              the strip: hide the key line;  --log FILE (or CASECLOCK_LOG): the seam
  --root DIR             --selftest: where floor/ and tests/ live

keys, in the strip: Ctrl+L fact . Ctrl+E explain . Ctrl+S sign-out . Ctrl+O case . Ctrl+T pin . Ctrl+Tab case . Esc
what it never does: reach the network (the build proves it), read or write the EDR, decide anything, run a model.
SYNTHETIC cases only in this repository. MIT. https://opnaorta.ai
)HELP";

static std::string need(int argc, char** argv, int& i, const char* flag) {
    if (i + 1 >= argc) { fprintf(stderr, "caseclock: %s needs a value\n", flag); exit(2); }
    return argv[++i];
}

int app_main(int argc, char** argv) {
    Opts o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.size() < 2 || a[0] != '-') { o.cases.push_back(a); continue; }
        if (a == "--json" || a == "-j") { if (o.mode == Opts::Mode::Auto) o.mode = Opts::Mode::Json; o.json = true; }
        else if (a == "--all") o.all = true;
        else if (a == "--signout") o.mode = Opts::Mode::Signout;
        else if (a == "--explain") { o.mode = Opts::Mode::Explain; o.explain_event = need(argc, argv, i, "--explain"); }
        else if (a == "--fact") { o.mode = Opts::Mode::Fact; o.fact_event = need(argc, argv, i, "--fact"); o.fact_time = need(argc, argv, i, "--fact EVENT"); }
        else if (a == "--report") o.mode = Opts::Mode::Report;
        else if (a == "--replay") { o.replay = true; if (o.mode == Opts::Mode::Auto) o.mode = Opts::Mode::Replay; }
        else if (a == "--export") { o.mode = Opts::Mode::Export; o.export_path = need(argc, argv, i, "--export"); }
        else if (a == "--verify") { o.mode = Opts::Mode::Verify; }
        else if (a == "--spool") o.mode = Opts::Mode::Spool;
        else if (a == "--mcp") o.mode = Opts::Mode::Mcp;
        else if (a == "--about") o.mode = Opts::Mode::About;
        else if (a == "--selftest") o.mode = Opts::Mode::Selftest;
        else if (a == "--gui") o.mode = Opts::Mode::Gui;
        else if (a == "--rules") o.rules.push_back(need(argc, argv, i, "--rules"));
        else if (a == "--now") { const std::string v = need(argc, argv, i, "--now"); if (!parse_hhmm(v, o.now)) { fprintf(stderr, "caseclock: --now '%s' is not minutes or HH:MM [+/-Nd]\n", v.c_str()); return 2; } o.has_now = true; }
        else if (a == "--tape") o.tape = need(argc, argv, i, "--tape");
        else if (a == "--clear") o.clear = true;
        else if (a == "--by") o.by = need(argc, argv, i, "--by");
        else if (a == "--next") o.next = need(argc, argv, i, "--next");
        else if (a == "--only") o.only = need(argc, argv, i, "--only");
        else if (a == "--speed") o.speed = (int)std::clamp(strtol(need(argc, argv, i, "--speed").c_str(), nullptr, 10), 0L, 100000L);
        else if (a == "--frames") o.frames = (int)std::clamp(strtol(need(argc, argv, i, "--frames").c_str(), nullptr, 10), 0L, 1000000L);
        else if (a == "--interval") o.interval_ms = (int)std::clamp(strtol(need(argc, argv, i, "--interval").c_str(), nullptr, 10), 100L, 3600000L);
        else if (a == "--spool-file") o.spool_file = need(argc, argv, i, "--spool-file");
        else if (a == "--lane") o.lane = need(argc, argv, i, "--lane");
        else if (a == "--shot") o.shot = need(argc, argv, i, "--shot");
        else if (a == "--no-activate") o.no_activate = true;
        else if (a == "--no-keys") o.no_keys = true;
        else if (a == "--log") o.log = need(argc, argv, i, "--log");
        else if (a == "--root") o.root = need(argc, argv, i, "--root");
        else if (a == "--quiet" || a == "-q") o.quiet = true;
        else if (a == "--plain") o.plain = true;
        else if (a == "-h" || a == "--help" || a == "/?") o.mode = Opts::Mode::Help;
        else if (a == "-v" || a == "--version") o.mode = Opts::Mode::Version;
        else { fprintf(stderr, "caseclock: unknown option '%s' (try --help)\n", a.c_str()); return 2; }
    }
    if (o.mode == Opts::Mode::Auto) o.mode = o.cases.empty() ? Opts::Mode::Help : Opts::Mode::Gui;

    if (o.mode == Opts::Mode::Gui) {
        Loaded L;
        std::string err;
        if (fresh_own_console()) free_console();
        attach_parent_console();
        if (o.replay) {
            // the strip over a replay: the case's facts arrive at N× on a scratch tape (the capture)
            for (const std::string& p : o.rules) {
                RulePack pk;
                if (!load_rule_pack(p, pk, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
                L.packs.push_back(std::move(pk));
            }
            for (const std::string& path : o.cases) {
                CaseDoc doc;
                if (!load_case_file(path, doc, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
                if (doc.id.empty()) doc.id = base_name(path);
                const ReplayPlan plan = plan_replay(doc, o.has_now, o.now);
                const std::string scratch = !o.tape.empty() ? o.tape : path_join(path_join(temp_dir(), "caseclock-replay"), sanitize_file_name(doc.id) + ".tape.jsonl");
                make_dirs(dir_name(scratch));
                remove_file(scratch);
                auto rt = std::make_unique<CaseRuntime>();
                rt->by = o.by.empty() ? "replay" : o.by;
                rt->replaying = true;
                rt->replay_start = plan.start;
                rt->replay_end = plan.end;
                rt->scheduled = plan.later;
                rt->clock_source = ssprintf("replay at %d\xC3\x97", o.speed);
                if (!rt->load(plan.at_start, L.packs, scratch, false, plan.start, &err)) { fprintf(stderr, "caseclock: %s\n", err.c_str()); return 2; }
                L.cases.push_back(std::move(rt));
            }
            if (L.cases.empty()) { fprintf(stderr, "caseclock: --replay needs a case file\n"); return 2; }
        } else if (!load_all(o, L, true, &err)) {
            fprintf(stderr, "caseclock: %s\n", err.c_str());
            return 2;
        }
        return run_gui(o, L.cases);
    }
    attach_parent_console();
    stdout_binary();
    console_utf8();
    switch (o.mode) {
        case Opts::Mode::Help: printf(kHelp, kVersion); return 0;
        case Opts::Mode::Version: printf("caseclock %s\n", kVersion); return 0;
        case Opts::Mode::About: return run_about();
        case Opts::Mode::Selftest: return run_selftest(o);
        case Opts::Mode::Report: return run_report(o);
        case Opts::Mode::Json: return run_json(o);
        case Opts::Mode::Explain: return run_explain(o);
        case Opts::Mode::Signout: return run_signout(o, true);
        case Opts::Mode::Fact: return run_fact(o);
        case Opts::Mode::Export: return run_export(o);
        case Opts::Mode::Verify: return run_verify(o);
        case Opts::Mode::Replay: return run_replay(o);
        case Opts::Mode::Spool: return run_spool(o);
        case Opts::Mode::Mcp: return run_mcp(o);
        default: return 2;
    }
}

}  // namespace caseclock

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    std::vector<char*> ptrs;
    for (int i = 0; i < argc; ++i) args.push_back(caseclock::narrow(argv[i]));
    for (auto& s : args) ptrs.push_back(s.data());
    return caseclock::app_main(argc, ptrs.data());
}
