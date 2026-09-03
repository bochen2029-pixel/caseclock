// caseclock · clock.cpp — see clock.h.
#include "clock.h"

#include "app_util.h"
#include "hash.h"
#include "sys.h"

#include <algorithm>

namespace caseclock {

int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q -= 1;
    return q;
}
int64_t floor_mod(int64_t a, int64_t b) { return a - floor_div(a, b) * b; }

int64_t ClockConfig::max_lead() const {
    int64_t m = 0;
    for (int64_t l : leads) m = std::max(m, l);
    return m;
}
int64_t ClockConfig::min_lead() const {
    int64_t m = INF;
    for (int64_t l : leads) m = std::min(m, l);
    return m == INF ? 0 : m;
}

std::string reason_text(const std::string& reason) {
    if (reason == "breach") return "BREACHED";
    if (reason == "due") return "due now";
    if (reason == "infeasible") return "INFEASIBLE";
    if (reason == "withdrawn") return "withdrawn";
    if (starts_with(reason, "lead_")) return "due in " + reason.substr(5) + " min";
    return reason;
}

static bool timer_reason(const std::string& r) { return r == "breach" || r == "due" || starts_with(r, "lead_"); }

// ── the fold ────────────────────────────────────────────────────────────────
void fold_step(ClockState& s, const Entry& e) {
    const Json& b = e.body;
    if (e.kind == "fact") {
        FactSpec f;
        f.event = b.str("event");
        f.minutes = b.num("minutes");
        f.by = b.str("by");
        f.entered_at = b.str("entered_at");
        f.has_entered = true;
        f.entered = e.at;
        s.facts.push_back(f);
        s.fact_seq[f.event] = e.seq;
        s.rows_fact++;
    } else if (e.kind == "derived") {
        Bound bd;
        bd.latest = b.num("latest", INF);
        bd.earliest = b.num("earliest", -INF);
        bd.chain = b.str_list("chain");
        s.derived[b.str("event")] = bd;
        s.said[b.str("event")].clear();
        s.infeasible_key.clear();
    } else if (e.kind == "withdrawn") {
        const Json* l = b.get("latest");
        if (!l || l->is_null()) { s.derived.erase(b.str("event")); s.said[b.str("event")].clear(); }
        s.rows_withdrawn++;
    } else if (e.kind == "said") {
        s.rows_said++;
        const std::string r = b.str("reason");
        if (timer_reason(r)) { s.last_said_at = e.at; s.said[b.str("event")].insert(r); }
        else if (r == "infeasible") s.infeasible_key = b.str("key");
    } else if (e.kind == "held") {
        s.rows_held++;
    } else if (e.kind == "silence") {
        s.rows_silence++;
        s.silence_hours.insert(b.num("hour"));
    } else if (e.kind == "infeasible") {
        s.rows_infeasible++;
        s.infeasible_key = b.str("key");
    } else if (e.kind == "signout") {
        s.last_signout_seq = e.seq;
    } else if (e.kind == "rules") {
        s.last_rules_seq = e.seq;
        s.last_rules_hash = b.str("rules_hash");
    } else if (e.kind == "note") {
        if (b.flag("feasible")) s.infeasible_key.clear();
    }
}
ClockState fold_clock(const Tape& tape) {
    ClockState s;
    for (const Entry& e : tape.entries()) fold_step(s, e);
    return s;
}

// ── rows ────────────────────────────────────────────────────────────────────
const Entry& CaseRuntime::row(const std::string& kind, int64_t at, Json body) {
    const Entry& e = tape.append(kind, at, std::move(body));
    fold_step(state, e);
    log.push_back(ssprintf("row %lld %s at %lld: ", (long long)e.seq, kind.c_str(), (long long)at) + e.body.canonical());
    return e;
}
const Entry& CaseRuntime::correction(int64_t supersedes, const std::string& kind, int64_t at, Json body) {
    const Entry& e = tape.correct(supersedes, kind, at, std::move(body));
    fold_step(state, e);
    log.push_back(ssprintf("row %lld %s at %lld (supersedes %lld): ", (long long)e.seq, kind.c_str(), (long long)at, (long long)supersedes) + e.body.canonical());
    return e;
}

bool CaseRuntime::flush(std::string* err) {
    if (!persist || observe_only || tape_path.empty()) { persisted_ = tape.size(); return true; }
    if (persisted_ >= tape.size()) return true;
    if (!tape.append_file(tape_path, encrypt, persisted_, err)) return false;
    persisted_ = tape.size();
    return true;
}

// ── loading ─────────────────────────────────────────────────────────────────
static std::vector<FactSpec> binding_facts(const std::vector<FactSpec>& in) {
    // the latest fact per event binds; order of first appearance
    std::vector<FactSpec> out;
    for (const FactSpec& f : in) {
        bool found = false;
        for (FactSpec& o : out)
            if (o.event == f.event) { o = f; found = true; break; }
        if (!found) out.push_back(f);
    }
    return out;
}

bool CaseRuntime::reclose(std::string* err) {
    if (!build_stn(packs, doc, binding_facts(state.facts), stn, err)) return false;
    closure = stn.close();
    return true;
}

bool CaseRuntime::load(const CaseDoc& d, const std::vector<RulePack>& p, const std::string& tape_file, bool encrypt_tape, int64_t now_minute, std::string* err) {
    doc = d;
    packs = p;
    tape_path = tape_file;
    encrypt = encrypt_tape;
    now = now_minute;
    for (const RulePack& pk : packs)
        if (pk.has_leads && !pk.lead_minutes.empty()) cfg.leads = pk.lead_minutes;
    std::sort(cfg.leads.begin(), cfg.leads.end(), std::greater<int64_t>());
    {
        std::string h;
        for (const RulePack& pk : packs) h += pk.sha256 + "\n";
        h += doc.sha256;
        rules_hash = sha256_hex(h);
    }
    const std::string id = doc.id.empty() ? base_name(doc.path) : doc.id;
    if (has_preload) {
        if (preload.case_id != id) { if (err) *err = "the tape belongs to case '" + preload.case_id + "', not '" + id + "'"; return false; }
        tape = preload;
    } else if (!tape_path.empty() && file_exists(tape_path)) {
        Tape t;
        bool enc = false;
        if (!Tape::load_file(tape_path, t, &enc, err)) return false;
        if (t.case_id != id) { if (err) *err = "the tape at " + tape_path + " belongs to case '" + t.case_id + "', not '" + id + "'"; return false; }
        tape = t;
        encrypt = enc;
    } else {
        tape = Tape(id);
    }
    state = fold_clock(tape);
    persisted_ = tape.size();
    if (observe_only) {
        // read the record as it is; the file's facts not yet on the tape bind in memory, so the
        // answer matches what the strip will show once it has written them
        for (const FactSpec& f : doc.facts) {
            bool present = false;
            for (const FactSpec& t : state.facts)
                if (t.event == f.event && t.minutes == f.minutes && t.by == f.by) { present = true; break; }
            if (present) continue;
            state.facts.push_back(f);
            state.fact_seq[f.event] = -1;
        }
        return reclose(err);
    }

    if (tape.empty() && !doc.note.empty()) {
        Json b = Json::object();
        b.set("text", Json::string(doc.note));
        if (doc.synthetic) b.set("synthetic", Json::boolean(true));
        row("note", now, b);
    }
    if (state.last_rules_hash != rules_hash) {
        Json b = Json::object();
        b.set("layers", Json::strings(layers()));
        Json pk = Json::array();
        for (const RulePack& x : packs) {
            Json o = Json::object();
            o.set("name", Json::string(x.name));
            o.set("layer", Json::string(x.layer));
            o.set("sha256", Json::string(x.sha256));
            pk.push(o);
        }
        b.set("packs", pk);
        Json cs = Json::object();
        cs.set("file", Json::string(base_name(doc.path)));
        cs.set("sha256", Json::string(doc.sha256));
        b.set("case", cs);
        b.set("verified_by", doc.verified_by.empty() ? Json::null() : Json::string(doc.verified_by));
        b.set("verified_on", doc.verified_on.empty() ? Json::null() : Json::string(doc.verified_on));
        Json leads = Json::array();
        for (int64_t l : cfg.leads) leads.push(Json::integer(l));
        b.set("lead_minutes", leads);
        b.set("rules_hash", Json::string(rules_hash));
        row("rules", now, b);
    }
    // the case file's facts, those not already on the tape
    for (const FactSpec& f : doc.facts) {
        bool present = false;
        for (const FactSpec& t : state.facts)
            if (t.event == f.event && t.minutes == f.minutes && t.by == f.by) { present = true; break; }
        if (present) continue;
        Json b = Json::object();
        b.set("event", Json::string(f.event));
        b.set("minutes", Json::integer(f.minutes));
        b.set("by", Json::string(f.by));
        b.set("entered_at", Json::string(f.has_entered ? hhmm(f.entered) : hhmm(now)));
        b.set("from", Json::string("case file"));
        const int64_t at = f.has_entered ? f.entered : now;
        auto it = state.fact_seq.find(f.event);
        if (it != state.fact_seq.end()) correction(it->second, "fact", at, b);
        else row("fact", at, b);
    }
    if (!reclose(err)) return false;
    Json cause = Json::object();
    cause.set("kind", Json::string("load"));
    derive(cause);
    speak(true);
    return flush(err);
}

// ── a fact ──────────────────────────────────────────────────────────────────
bool CaseRuntime::add_fact(const std::string& event, int64_t minutes, const std::string& who, int64_t at, std::string* err) {
    if (event.empty()) { if (err) *err = "a fact needs an event name"; return false; }
    now = at;
    Json b = Json::object();
    b.set("event", Json::string(event));
    b.set("minutes", Json::integer(minutes));
    b.set("by", Json::string(who));
    b.set("entered_at", Json::string(hhmm(at)));
    int64_t seq;
    auto it = state.fact_seq.find(event);
    if (it != state.fact_seq.end()) seq = correction(it->second, "fact", at, b).seq;
    else seq = row("fact", at, b).seq;
    if (!reclose(err)) return false;
    Json cause = Json::object();
    cause.set("kind", Json::string("fact"));
    cause.set("event", Json::string(event));
    cause.set("minutes", Json::integer(minutes));
    cause.set("seq", Json::integer(seq));
    derive(cause);
    speak(true);
    return flush(err);
}

void CaseRuntime::tick(int64_t now_minute) {
    if (now_minute == now) return;
    now = now_minute;
    speak(false);
}

bool CaseRuntime::apply_scheduled(int64_t minute, std::string* err) {
    for (const FactSpec& f : scheduled)
        if (f.entered == minute && !add_fact(f.event, f.minutes, f.by.empty() ? by : f.by, minute, err)) return false;
    return true;
}

ReplayPlan plan_replay(const CaseDoc& doc, bool has_end, int64_t end_override) {
    ReplayPlan p;
    p.start = doc.has_now ? doc.now : 0;
    p.end = p.start;
    for (const ReplayLine& r : doc.replay) { p.start = std::min(p.start, r.at); p.end = std::max(p.end, r.at); }
    for (const FactSpec& f : doc.facts)
        if (f.has_entered) { p.start = std::min(p.start, f.entered); p.end = std::max(p.end, f.entered); }
    if (has_end) p.end = end_override;
    p.at_start = doc;
    p.at_start.facts.clear();
    for (const FactSpec& f : doc.facts) {
        if (f.has_entered && f.entered > p.start) p.later.push_back(f);
        else p.at_start.facts.push_back(f);
    }
    return p;
}

bool CaseRuntime::record_signout(const std::string& text, const std::string& who, const std::string& next, std::string* err) {
    Json b = Json::object();
    b.set("by", Json::string(who));
    b.set("next", Json::string(next));
    b.set("hash", Json::string(sha256_hex(text)));
    b.set("rows", Json::integer((int64_t)tape.size()));
    row("signout", now, b);
    return flush(err);
}

// ── views ───────────────────────────────────────────────────────────────────
std::vector<std::string> CaseRuntime::watched() const {
    if (!doc.watch.empty()) return doc.watch;
    std::vector<std::string> out;
    for (const std::string& n : stn.names)
        if (n != REFERENCE) out.push_back(n);
    return out;
}

std::vector<Deadline> CaseRuntime::open() const {
    std::vector<Deadline> out;
    if (infeasible()) return out;
    for (const std::string& e : watched()) {
        if (!closure.has(e)) continue;
        if (state.fact_seq.count(e)) continue;   // entered: no longer a deadline
        const int64_t l = closure.latest(e);
        if (!finite_bound(l)) continue;
        Deadline d;
        d.event = e;
        d.latest = l;
        d.earliest = closure.earliest(e);
        d.slack = l - now;
        d.chain = closure.binding_path(e);
        out.push_back(std::move(d));
    }
    std::stable_sort(out.begin(), out.end(), [](const Deadline& a, const Deadline& b) { return a.latest != b.latest ? a.latest < b.latest : a.event < b.event; });
    return out;
}
bool CaseRuntime::nearest(Deadline& out) const {
    const std::vector<Deadline> dl = open();
    if (dl.empty()) return false;
    out = dl.front();
    return true;
}
bool CaseRuntime::deadline_for(const std::string& event, Deadline& out) const {
    for (const Deadline& d : open())
        if (d.event == event) { out = d; return true; }
    return false;
}

std::vector<std::string> CaseRuntime::layers() const {
    std::set<std::string> s;
    for (const RulePack& p : packs) {
        if (!p.layer.empty()) s.insert(p.layer);
        for (const ConstraintSpec& c : p.constraints)
            if (!c.layer.empty()) s.insert(c.layer);
    }
    for (const ConstraintSpec& c : doc.constraints)
        if (!c.layer.empty()) s.insert(c.layer);
    if (!state.facts.empty() || !doc.facts.empty()) s.insert("L4");
    return std::vector<std::string>(s.begin(), s.end());
}
std::string CaseRuntime::layers_text() const {
    std::string t = "layers";
    const std::vector<std::string> ls = layers();
    if (ls.empty()) t += " none";
    for (const std::string& l : ls) t += " " + l;
    t += " \xC2\xB7 packs ";
    if (packs.empty()) t += "none (the case carries its own rules)";
    else {
        for (size_t i = 0; i < packs.size(); ++i) t += (i ? " " : "") + packs[i].name;
    }
    return t;
}

std::string CaseRuntime::slack_text(int64_t slack) const {
    if (slack < 0) return ssprintf("BREACHED %lld min", (long long)-slack);
    if (slack == 0) return "due now";
    return ssprintf("%lld min", (long long)slack);
}
std::string CaseRuntime::line_for(const Deadline& d) const {
    return doc.id + " \xC2\xB7 " + d.event + " by " + hhmm(d.latest) + " \xC2\xB7 " + slack_text(d.slack) + ssprintf(" \xC2\xB7 chain %zu", d.chain.size());
}
std::string CaseRuntime::cycle_text(const std::vector<std::string>& cyc, int64_t short_by) const {
    std::string t = doc.id + ssprintf(" \xC2\xB7 INFEASIBLE \xC2\xB7 short by %lld min \xC2\xB7 ", (long long)short_by);
    for (size_t i = 0; i < cyc.size(); ++i) { if (i) t += " \xE2\x86\x92 "; t += cyc[i]; }
    return t;
}
std::string CaseRuntime::strip_text() const {
    if (infeasible()) {
        const std::vector<std::string> cyc = closure.negative_cycle();
        int64_t total = 0;
        for (const Constraint& c : closure.cycle_constraints(cyc)) total += c.weight;
        return cycle_text(cyc, -total);
    }
    Deadline d;
    if (nearest(d)) return line_for(d);
    return doc.id + ssprintf(" \xC2\xB7 nothing due \xC2\xB7 %lld facts \xC2\xB7 %zu rows", (long long)state.rows_fact, tape.size());
}

CaseRuntime::Mood CaseRuntime::mood() const {
    if (infeasible()) return Mood::Violet;
    Deadline d;
    if (!nearest(d)) return Mood::Quiet;
    if (d.slack < 0 || d.slack <= std::min<int64_t>(15, cfg.max_lead())) return Mood::Red;
    if (d.slack <= cfg.max_lead()) return Mood::Amber;
    return Mood::Quiet;
}

std::string CaseRuntime::explain(const std::string& event) const {
    if (!closure.has(event)) return event + ": not an event of this case";
    if (infeasible()) return strip_text();
    return closure.explain(event);
}

Json CaseRuntime::to_json() const {
    Json j = Json::object();
    j.set("case", Json::string(doc.id));
    j.set("file", Json::string(doc.path));
    j.set("synthetic", Json::boolean(doc.synthetic));
    j.set("now", Json::integer(now));
    j.set("now_hhmm", Json::string(hhmm(now)));
    j.set("consistent", Json::boolean(!infeasible()));
    j.set("line", Json::string(strip_text()));
    if (infeasible()) {
        const std::vector<std::string> cyc = closure.negative_cycle();
        int64_t total = 0;
        Json cs = Json::array();
        for (const Constraint& c : closure.cycle_constraints(cyc)) { total += c.weight; cs.push(Json::string(c.render())); }
        Json inf = Json::object();
        inf.set("cycle", Json::strings(cyc));
        inf.set("constraints", cs);
        inf.set("short_by", Json::integer(-total));
        j.set("infeasible", inf);
    }
    Json open_ = Json::array();
    for (const Deadline& d : open()) {
        Json o = Json::object();
        o.set("event", Json::string(d.event));
        o.set("latest", Json::integer(d.latest));
        o.set("latest_hhmm", Json::string(hhmm(d.latest)));
        o.set("earliest", finite_bound(d.earliest) ? Json::integer(d.earliest) : Json::null());
        o.set("slack", Json::integer(d.slack));
        o.set("breached", Json::boolean(d.slack < 0));
        Json ch = Json::array();
        for (const Constraint& c : d.chain) ch.push(Json::string(c.render()));
        o.set("chain", ch);
        open_.push(o);
    }
    j.set("open", open_);
    Json facts = Json::array();
    for (const FactSpec& f : binding_facts(state.facts)) {
        Json o = Json::object();
        o.set("event", Json::string(f.event));
        o.set("minutes", Json::integer(f.minutes));
        o.set("hhmm", Json::string(hhmm(f.minutes)));
        o.set("by", Json::string(f.by));
        facts.push(o);
    }
    j.set("facts", facts);
    Json rules = Json::object();
    rules.set("layers", Json::strings(layers()));
    rules.set("hash", Json::string(rules_hash));
    Json pk = Json::array();
    for (const RulePack& x : packs) pk.push(Json::string(x.name));
    rules.set("packs", pk);
    Json leads = Json::array();
    for (int64_t l : cfg.leads) leads.push(Json::integer(l));
    rules.set("lead_minutes", leads);
    rules.set("verified_by", doc.verified_by.empty() ? Json::null() : Json::string(doc.verified_by));
    rules.set("verified_on", doc.verified_on.empty() ? Json::null() : Json::string(doc.verified_on));
    j.set("rules", rules);
    Json tp = Json::object();
    tp.set("head", Json::string(tape.head()));
    tp.set("rows", Json::integer((int64_t)tape.size()));
    tp.set("said", Json::integer(state.rows_said));
    tp.set("held", Json::integer(state.rows_held));
    tp.set("withdrawn", Json::integer(state.rows_withdrawn));
    tp.set("silence", Json::integer(state.rows_silence));
    tp.set("path", tape_path.empty() ? Json::null() : Json::string(tape_path));
    tp.set("encrypted", Json::boolean(encrypt && !tape_path.empty()));
    j.set("tape", tp);
    return j;
}

// ── derived / withdrawn / infeasible ────────────────────────────────────────
void CaseRuntime::derive(const Json& cause) {
    if (infeasible()) {
        const std::vector<std::string> cyc = closure.negative_cycle();
        const std::vector<Constraint> cons = closure.cycle_constraints(cyc);
        int64_t total = 0;
        Json cs = Json::array();
        for (const Constraint& c : cons) { total += c.weight; cs.push(Json::string(c.render())); }
        std::string key;
        for (const std::string& n : cyc) key += n + ">";
        key += std::to_string(-total);
        // every derived bound is void now
        for (const auto& kv : std::map<std::string, Bound>(state.derived)) {
            Json w = Json::object();
            w.set("event", Json::string(kv.first));
            w.set("previous", Json::integer(kv.second.latest));
            w.set("latest", Json::null());
            w.set("cause", cause);
            const bool was_said = !state.said[kv.first].empty();
            row("withdrawn", now, w);
            if (was_said) {
                Json s = Json::object();
                s.set("event", Json::string(kv.first));
                s.set("reason", Json::string("withdrawn"));
                s.set("previous", Json::integer(kv.second.latest));
                s.set("text", Json::string(doc.id + " \xC2\xB7 " + kv.first + " was by " + hhmm(kv.second.latest) + " \xC2\xB7 withdrawn: the plan is infeasible"));
                row("said", now, s);
            }
        }
        if (state.infeasible_key != key) {
            Json b = Json::object();
            b.set("cycle", Json::strings(cyc));
            b.set("constraints", cs);
            b.set("short_by", Json::integer(-total));
            b.set("key", Json::string(key));
            b.set("cause", cause);
            row("infeasible", now, b);
            Json s = Json::object();
            s.set("reason", Json::string("infeasible"));
            s.set("key", Json::string(key));
            s.set("short_by", Json::integer(-total));
            s.set("cycle", Json::strings(cyc));
            s.set("text", Json::string(cycle_text(cyc, -total)));
            row("said", now, s);
        }
        return;
    }
    if (!state.infeasible_key.empty()) {
        Json b = Json::object();
        b.set("text", Json::string("the plan is feasible again"));
        b.set("feasible", Json::boolean(true));
        b.set("cause", cause);
        row("note", now, b);
    }
    for (const std::string& e : watched()) {
        if (!closure.has(e)) continue;
        const int64_t l = closure.latest(e), er = closure.earliest(e);
        auto prev = state.derived.find(e);
        const bool had = prev != state.derived.end();
        const Bound before = had ? prev->second : Bound();
        if (finite_bound(l) && !state.fact_seq.count(e)) {
            std::vector<std::string> chain;
            for (const Constraint& c : closure.binding_path(e)) chain.push_back(c.render());
            if (!had || before.latest != l || before.earliest != er || before.chain != chain) {
                const bool was_said = had && !state.said[e].empty();
                Json b = Json::object();
                b.set("event", Json::string(e));
                b.set("latest", Json::integer(l));
                b.set("earliest", finite_bound(er) ? Json::integer(er) : Json::integer(-INF));
                b.set("chain", Json::strings(chain));
                b.set("rules_hash", Json::string(rules_hash));
                b.set("cause", cause);
                row("derived", now, b);
                if (had && before.latest != l) {
                    Json w = Json::object();
                    w.set("event", Json::string(e));
                    w.set("previous", Json::integer(before.latest));
                    w.set("latest", Json::integer(l));
                    w.set("cause", cause);
                    row("withdrawn", now, w);
                    if (was_said) {
                        Json s = Json::object();
                        s.set("event", Json::string(e));
                        s.set("reason", Json::string("withdrawn"));
                        s.set("previous", Json::integer(before.latest));
                        s.set("latest", Json::integer(l));
                        s.set("text", Json::string(doc.id + " \xC2\xB7 " + e + " was by " + hhmm(before.latest) + " \xC2\xB7 now by " + hhmm(l)));
                        row("said", now, s);
                    }
                }
            }
        } else if (had) {
            const bool was_said = !state.said[e].empty();
            Json w = Json::object();
            w.set("event", Json::string(e));
            w.set("previous", Json::integer(before.latest));
            w.set("latest", Json::null());
            w.set("cause", cause);
            row("withdrawn", now, w);
            if (was_said) {
                Json s = Json::object();
                s.set("event", Json::string(e));
                s.set("reason", Json::string("withdrawn"));
                s.set("previous", Json::integer(before.latest));
                s.set("text", Json::string(doc.id + " \xC2\xB7 " + e + " was by " + hhmm(before.latest) + " \xC2\xB7 " + (state.fact_seq.count(e) ? "entered" : "no longer bound")));
                row("said", now, s);
            }
        }
    }
}

// ── the timer rules ─────────────────────────────────────────────────────────
std::string CaseRuntime::reason_for(int64_t slack) const {
    if (slack < 0) return "breach";
    std::vector<int64_t> asc = cfg.leads;
    std::sort(asc.begin(), asc.end());
    for (int64_t l : asc)
        if (slack <= l) return l == 0 ? "due" : "lead_" + std::to_string(l);
    return "";
}
static int reason_rank(const std::string& r) {
    if (r == "breach") return -2;
    if (r == "due") return -1;
    if (starts_with(r, "lead_")) return (int)strtol(r.c_str() + 5, nullptr, 10);
    return 1 << 20;
}

void CaseRuntime::speak(bool after_closure) {
    if (infeasible()) return;
    const std::vector<Deadline> dl = open();
    struct Cand { Deadline d; std::string reason; };
    std::vector<Cand> cands;
    for (const Deadline& d : dl) {
        const std::string r = reason_for(d.slack);
        if (r.empty()) continue;
        auto it = state.said.find(d.event);
        if (it != state.said.end() && it->second.count(r)) continue;
        cands.push_back(Cand{d, r});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        const int ra = reason_rank(a.reason), rb = reason_rank(b.reason);
        if (ra != rb) return ra < rb;
        if (a.d.latest != b.d.latest) return a.d.latest < b.d.latest;
        return a.d.event < b.d.event;
    });
    if (cands.empty()) {
        const int64_t hour = floor_div(now, 60);
        bool due_soon = false;
        for (const Deadline& d : dl)
            if (d.slack <= cfg.max_lead()) { due_soon = true; break; }
        if (floor_mod(now, 60) == 0 && !state.silence_hours.count(hour) && !due_soon) {
            Json b = Json::object();
            b.set("hour", Json::integer(hour));
            b.set("open", Json::integer((int64_t)dl.size()));
            b.set("next", dl.empty() ? Json::null() : Json::integer(dl.front().latest));
            b.set("next_event", dl.empty() ? Json::null() : Json::string(dl.front().event));
            row("silence", now, b);
        } else if (after_closure) {
            Json b = Json::object();
            b.set("reason", Json::string("lead_not_reached"));
            if (!dl.empty()) {
                b.set("event", Json::string(dl.front().event));
                b.set("latest", Json::integer(dl.front().latest));
                b.set("slack", Json::integer(dl.front().slack));
                b.set("next_lead_at", Json::integer(dl.front().latest - cfg.max_lead()));
            }
            b.set("open", Json::integer((int64_t)dl.size()));
            row("held", now, b);
        }
        return;
    }
    const Cand& top = cands.front();
    if (state.last_said_at == now) {
        Json b = Json::object();
        b.set("reason", Json::string("rate"));
        b.set("event", Json::string(top.d.event));
        b.set("would_say", Json::string(top.reason));
        b.set("latest", Json::integer(top.d.latest));
        b.set("slack", Json::integer(top.d.slack));
        row("held", now, b);
        return;
    }
    {
        Json s = Json::object();
        s.set("event", Json::string(top.d.event));
        s.set("latest", Json::integer(top.d.latest));
        s.set("slack", Json::integer(top.d.slack));
        s.set("reason", Json::string(top.reason));
        Json ch = Json::array();
        for (const Constraint& c : top.d.chain) ch.push(Json::string(c.render()));
        s.set("chain", ch);
        s.set("text", Json::string(line_for(top.d)));
        row("said", now, s);
    }
    for (size_t i = 1; i < cands.size(); ++i) {
        Json b = Json::object();
        b.set("reason", Json::string("next_due"));
        b.set("event", Json::string(cands[i].d.event));
        b.set("would_say", Json::string(cands[i].reason));
        b.set("latest", Json::integer(cands[i].d.latest));
        b.set("slack", Json::integer(cands[i].d.slack));
        b.set("behind", Json::string(top.d.event));
        row("held", now, b);
    }
}

}  // namespace caseclock
