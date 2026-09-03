// caseclock · signout.cpp — see signout.h.
#include "signout.h"

#include "app_util.h"

#include <cstring>
#include <map>

namespace caseclock {

static const char* kDot = " \xC2\xB7 ";   // " · "

std::string render_chain(const std::vector<Constraint>& chain, const std::string& indent) {
    std::string s;
    int64_t running = 0;
    for (const Constraint& c : chain) {
        running += c.weight;
        const std::string expr = c.later + " - " + c.earlier + " <= " + std::to_string(c.weight);
        const std::string layer = "[" + (c.layer.empty() ? std::string("--") : c.layer) + "]";
        s += indent + pad_cp(expr, 44) + pad_cp(layer, 6) + pad_cp(c.label, 50) + " cumulative " + hhmm(running) + "\n";
    }
    if (chain.empty()) s += indent + "(no chain: the bound is the reference itself)\n";
    return s;
}

std::string render_signout(const CaseRuntime& rt, const std::string& by, const std::string& next) {
    std::string s;
    const std::string id = rt.doc.id;
    s += "CASECLOCK" + std::string(kDot) + "SIGN-OUT" + kDot + id + kDot + hhmm(rt.now) + kDot + "by " + by + " \xE2\x86\x92 next: " + next + "\n";
    s += rt.layers_text() + kDot + "rules " + hex_short(rt.rules_hash);
    if (!rt.doc.verified_by.empty()) s += std::string(kDot) + "verified by " + rt.doc.verified_by + (rt.doc.verified_on.empty() ? std::string() : " on " + rt.doc.verified_on);
    else s += std::string(kDot) + "not verified";
    s += "\n";
    s += "tape " + hex_short(rt.tape.head()) + ssprintf(" \xC2\xB7 %zu rows \xC2\xB7 %lld said \xC2\xB7 %lld held \xC2\xB7 %lld withdrawn \xC2\xB7 %lld silent hours\n",
                                                        rt.tape.size(), (long long)rt.state.rows_said, (long long)rt.state.rows_held,
                                                        (long long)rt.state.rows_withdrawn, (long long)rt.state.rows_silence);
    s += "\n";

    // DUE, IN ORDER
    if (rt.infeasible()) {
        const std::vector<std::string> cyc = rt.closure.negative_cycle();
        int64_t total = 0;
        const std::vector<Constraint> cons = rt.closure.cycle_constraints(cyc);
        for (const Constraint& c : cons) total += c.weight;
        s += ssprintf("INFEASIBLE \xE2\x80\x94 this plan cannot be met, short by %lld min\n", (long long)-total);
        std::string line = "  ";
        for (size_t i = 0; i < cyc.size(); ++i) { if (i) line += " \xE2\x86\x92 "; line += cyc[i]; }
        s += line + "\n  these constraints cannot all hold:\n";
        for (const Constraint& c : cons) s += "    " + c.render() + "\n";
    } else {
        s += "DUE, IN ORDER\n";
        const std::vector<Deadline> dl = rt.open();
        if (dl.empty()) s += "  nothing is due: every watched event is entered or unbounded\n";
        for (const Deadline& d : dl) {
            s += "  " + pad_cp(d.event, 20) + pad_cp(hhmm(d.latest), 14) + pad_cp(rt.slack_text(d.slack), 18) + ssprintf("chain %zu", d.chain.size());
            if (d.slack < 0 && !d.chain.empty()) s += "   \xE2\x86\x92  cannot hold as planned: " + d.chain.front().label;
            s += "\n";
        }
    }
    s += "\n";

    // SINCE THE LAST SIGN-OUT
    const int64_t since = rt.state.last_signout_seq;
    if (since >= 0) s += "SINCE THE LAST SIGN-OUT (" + hhmm(rt.tape[(size_t)since].at) + ")\n";
    else s += "SINCE THE START OF THE TAPE\n";
    std::map<std::string, int64_t> held;
    int64_t silence = 0;
    size_t listed = 0;
    for (size_t i = (size_t)(since + 1); i < rt.tape.size(); ++i) {
        const Entry& e = rt.tape[i];
        const Json& b = e.body;
        if (e.kind == "held") { held[b.str("reason")]++; continue; }
        if (e.kind == "silence") { silence++; continue; }
        if (e.kind == "fact") {
            s += "  fact       " + pad_cp(hhmm(e.at), 13) + b.str("event") + " at " + hhmm(b.num("minutes")) + "   (entered by " + b.str("by") + (b.get("supersedes") ? ", corrects an earlier entry" : "") + ")\n";
            listed++;
        } else if (e.kind == "said") {
            std::string text = b.str("text");
            if (starts_with(text, id + kDot)) text = text.substr(id.size() + strlen(kDot));
            s += "  said       " + pad_cp(hhmm(e.at), 13) + text + "\n";
            listed++;
        } else if (e.kind == "withdrawn") {
            const Json* l = b.get("latest");
            s += "  withdrawn  " + pad_cp(hhmm(e.at), 13) + b.str("event") + " was by " + hhmm(b.num("previous")) + (l && l->is_int() ? " \xE2\x86\x92 now by " + hhmm(l->i) : std::string(" \xE2\x86\x92 no longer bound")) + "\n";
            listed++;
        } else if (e.kind == "infeasible") {
            s += "  infeasible " + pad_cp(hhmm(e.at), 13) + ssprintf("short by %lld min", (long long)b.num("short_by")) + "\n";
            listed++;
        } else if (e.kind == "rules") {
            s += "  rules      " + pad_cp(hhmm(e.at), 13) + "rule set " + hex_short(b.str("rules_hash")) + "\n";
            listed++;
        }
    }
    for (const auto& kv : held) {
        const char* why = kv.first == "rate" ? "waited for the one-line-a-minute rule" : kv.first == "next_due" ? "a nearer deadline was said first" : "nothing due within the lead";
        s += ssprintf("  held       \xC3\x97%-12lld", (long long)kv.second) + why + "\n";
    }
    if (silence) s += ssprintf("  silence    \xC3\x97%-12lld", (long long)silence) + "hour marks with nothing due within the lead\n";
    if (!listed && held.empty() && !silence) s += "  nothing\n";
    s += "\n";

    // THE CHAIN FOR …
    std::vector<std::string> chains = rt.doc.explain;
    if (chains.empty()) {
        Deadline d;
        if (rt.nearest(d)) chains.push_back(d.event);
    }
    if (!rt.infeasible()) {
        for (const std::string& ev : chains) {
            if (!rt.closure.has(ev)) continue;
            s += "THE CHAIN FOR " + ev + "   (latest " + hhmm(rt.closure.latest(ev)) + ")\n";
            s += render_chain(rt.closure.binding_path(ev));
            s += "\n";
        }
    }

    std::string foot = rt.doc.synthetic ? "SYNTHETIC CASE \xC2\xB7 NO PHI \xC2\xB7 every duration illustrative" : "derived by caseclock " + std::string(kVersion) + "; a human signs";
    s += pad_cp(foot, 60) + "signed: ______________________\n";
    return s;
}

}  // namespace caseclock
