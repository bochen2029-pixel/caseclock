// caseclock · casefile.cpp — see casefile.h.
#include "casefile.h"

#include "app_util.h"
#include "hash.h"
#include "sys.h"

namespace caseclock {

static bool need_int(const Json& j, const char* key, int64_t& out, std::string* err) {
    const Json* v = j.get(key);
    if (!v || !v->is_int()) { if (err) *err = std::string("constraint: missing integer '") + key + "'"; return false; }
    out = v->i;
    return true;
}
static bool need_str(const Json& j, const char* key, std::string& out, std::string* err) {
    const Json* v = j.get(key);
    if (!v || !v->is_str()) { if (err) *err = std::string("constraint: missing string '") + key + "'"; return false; }
    out = v->s;
    return true;
}

bool parse_constraint(const Json& j, ConstraintSpec& out, std::string* err) {
    out = ConstraintSpec();
    if (!j.is_obj()) { if (err) *err = "constraint: not an object"; return false; }
    if (!need_str(j, "kind", out.kind, err)) return false;
    out.label = j.str("label");
    out.layer = j.str("layer");
    out.source = j.str("source");
    if (out.kind == "at_least" || out.kind == "at_most") {
        return need_str(j, "later", out.later, err) && need_str(j, "earlier", out.earlier, err) && need_int(j, "minutes", out.minutes, err);
    }
    if (out.kind == "window") {
        return need_str(j, "event", out.event, err) && need_int(j, "opens", out.opens, err) && need_int(j, "closes", out.closes, err);
    }
    if (out.kind == "at") {
        return need_str(j, "event", out.event, err) && need_int(j, "minutes", out.minutes, err);
    }
    if (err) *err = "unknown constraint kind: '" + out.kind + "'";
    return false;
}

bool add_constraint(STN& stn, const ConstraintSpec& c, std::string* err) {
    if (c.kind == "at_least") stn.at_least(c.later, c.earlier, c.minutes, c.label, c.layer, c.source);
    else if (c.kind == "at_most") stn.at_most(c.later, c.earlier, c.minutes, c.label, c.layer, c.source);
    else if (c.kind == "window") stn.window(c.event, c.opens, c.closes, c.label, c.layer, c.source);
    else if (c.kind == "at") stn.at(c.event, c.minutes, c.label, c.layer, c.source);
    else { if (err) *err = "unknown constraint kind: '" + c.kind + "'"; return false; }
    return true;
}

static void parse_facts(const Json& doc, std::vector<FactSpec>& out) {
    const Json* fs = doc.get("facts");
    if (!fs || !fs->is_arr()) return;
    for (const Json& f : fs->arr) {
        if (!f.is_obj()) continue;
        FactSpec s;
        s.event = f.str("event");
        const Json* m = f.get("minutes");
        if (s.event.empty() || !m || !m->is_int()) continue;
        s.minutes = m->i;
        s.by = f.str("by");
        s.entered_at = f.str("entered_at");
        s.label = f.str("label");
        const Json* e = f.get("entered");
        if (e && e->is_int()) { s.has_entered = true; s.entered = e->i; }
        else if (!s.entered_at.empty() && parse_hhmm(s.entered_at, s.entered)) s.has_entered = true;
        out.push_back(std::move(s));
    }
}

bool load_case_text(const std::string& text, const std::string& path, CaseDoc& out, std::string* err) {
    out = CaseDoc();
    out.path = path;
    out.sha256 = sha256_hex(text);
    std::string jerr;
    if (!json_parse(text, out.raw, &jerr)) { if (err) *err = path + ": " + jerr; return false; }
    if (!out.raw.is_obj()) { if (err) *err = path + ": the case file is not a JSON object"; return false; }
    const Json& d = out.raw;
    out.id = d.str("id");
    out.synthetic = d.flag("synthetic");
    out.note = d.str("note");
    out.reference = d.str("reference");
    out.reference_at = d.str("reference_at");
    if (const Json* n = d.get("now"); n && n->is_int()) { out.has_now = true; out.now = n->i; }
    out.watch = d.str_list("watch");
    out.explain = d.str_list("explain");
    out.verified_by = d.str("verified_by");
    out.verified_on = d.str("verified_on");
    if (const Json* cs = d.get("constraints"); cs && cs->is_arr()) {
        for (const Json& c : cs->arr) {
            ConstraintSpec s;
            if (!parse_constraint(c, s, err)) { if (err) *err = path + ": " + *err; return false; }
            out.constraints.push_back(std::move(s));
        }
    }
    parse_facts(d, out.facts);
    if (const Json* r = d.get("replay"); r && r->is_arr())
        for (const Json& l : r->arr)
            if (l.is_obj() && l.get("at") && l.get("at")->is_int()) out.replay.push_back(ReplayLine{l.get("at")->i, l.str("say")});
    if (const Json* e = d.get("expected")) out.expected = *e;
    return true;
}

bool load_case_file(const std::string& path, CaseDoc& out, std::string* err) {
    std::string text;
    if (!read_file(path, text, err)) return false;
    return load_case_text(text, path, out, err);
}

bool load_rule_pack(const std::string& path, RulePack& out, std::string* err) {
    out = RulePack();
    out.path = path;
    std::string text;
    if (!read_file(path, text, err)) return false;
    out.sha256 = sha256_hex(text);
    Json d;
    std::string jerr;
    if (!json_parse(text, d, &jerr) || !d.is_obj()) { if (err) *err = path + ": " + (jerr.empty() ? "not an object" : jerr); return false; }
    out.name = d.str("name", base_name(path));
    out.layer = d.str("layer");
    out.verified_by = d.str("verified_by");
    out.verified_on = d.str("verified_on");
    if (const Json* l = d.get("lead_minutes"); l && l->is_arr()) {
        out.has_leads = true;
        for (const Json& v : l->arr)
            if (v.is_int()) out.lead_minutes.push_back(v.i);
    }
    if (const Json* cs = d.get("constraints"); cs && cs->is_arr()) {
        for (const Json& c : cs->arr) {
            ConstraintSpec s;
            if (!parse_constraint(c, s, err)) { if (err) *err = path + ": " + *err; return false; }
            if (s.layer.empty()) s.layer = out.layer;
            out.constraints.push_back(std::move(s));
        }
    }
    return true;
}

bool build_stn(const std::vector<RulePack>& packs, const CaseDoc& doc, const std::vector<FactSpec>& facts, STN& out, std::string* err) {
    out = STN();
    for (const RulePack& p : packs)
        for (const ConstraintSpec& c : p.constraints)
            if (!add_constraint(out, c, err)) return false;
    for (const ConstraintSpec& c : doc.constraints)
        if (!add_constraint(out, c, err)) return false;
    for (const FactSpec& f : facts) {
        const std::string label = !f.label.empty() ? f.label : (f.by.empty() ? std::string("entered") : "entered by " + f.by);
        out.at(f.event, f.minutes, label, "L4");
    }
    return true;
}

// ── the reference's report(), byte for byte ─────────────────────────────────
int report_text(const CaseDoc& doc, const STN& stn, const Closure& c, bool has_now, int64_t now, std::string& out) {
    out.clear();
    out += "case: " + (doc.raw.get("id") ? doc.id : doc.path) + "\n";
    if (!doc.note.empty()) out += "      " + doc.note + "\n";
    out += "\n";
    if (!c.consistent()) {
        const std::vector<std::string> cyc = c.negative_cycle();
        out += "INFEASIBLE \xE2\x80\x94 this plan cannot be met.\n";
        std::string line = "  ";
        for (size_t i = 0; i < cyc.size(); ++i) { if (i) line += " -> "; line += cyc[i]; }
        out += line + "\n";
        out += "  these constraints cannot all hold \xE2\x80\x94\n";
        int64_t total = 0;
        for (const Constraint& k : c.cycle_constraints(cyc)) {
            total += k.weight;
            out += "    " + pad_cp(k.render(), 62) + ssprintf(" running %+lldm\n", (long long)total);
        }
        out += "    " + std::string(62, ' ') + " " + rpad_cp("short by", 10) + ssprintf(" %lldm\n", (long long)-total);
        return 1;
    }
    std::vector<std::string> watch = doc.watch;
    if (watch.empty())
        for (const std::string& n : stn.names)
            if (n != REFERENCE) watch.push_back(n);
    out += pad_cp("event", 22) + rpad_cp("earliest", 10) + rpad_cp("latest", 12) + (has_now ? rpad_cp("slack", 10) : std::string()) + "\n";
    out += std::string((size_t)(44 + (has_now ? 10 : 0)), '-') + "\n";
    for (const std::string& name : watch) {
        const int64_t e = c.earliest(name), l = c.latest(name);
        std::string row = pad_cp(name, 22) + rpad_cp(hhmm(e), 10) + rpad_cp(hhmm(l), 12);
        if (has_now) {
            const int64_t s = c.slack(name, now);
            row += rpad_cp(std::to_string(s) + "m", 10) + (s < 0 ? "   BREACHED" : "");
        }
        out += row + "\n";
    }
    if (!doc.explain.empty()) {
        out += "\n";
        for (const std::string& name : doc.explain) {
            out += c.explain(name) + "\n";
            out += "\n";
        }
    }
    return 0;
}

}  // namespace caseclock
