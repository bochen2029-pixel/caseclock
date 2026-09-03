// caseclock · clock.h — a case at run time, and the speaking rules (gear zero).
//
// CaseRuntime holds one case: the rule packs, the case file, the facts, the tape, the closure. Every
// change is a row on the tape first; everything the speaking rules remember is a fold over the rows
// (ClockState), so a reopened tape resumes exactly, and a replay reproduces the same rows.
//
// The rules (BLUEPRINT §5, made precise here):
//   · a deadline speaks once per lead (60, 15, 0 minutes before, configurable), once on breach;
//     the most urgent unsaid reason wins; a bound that moves starts over;
//   · at most one timer-driven line per minute (others wait: a `held` row, reason `rate`);
//     a nearer deadline said this minute holds the others (`held`, reason `next_due`);
//   · after every closure (load, fact) that says nothing, one `held` row (`lead_not_reached`)
//     naming the nearest deadline — the trace that the tool looked and chose silence;
//   · infeasible and withdrawn lines are event-driven (a human just typed the fact) and are not
//     rate-limited; infeasible is said once per distinct cycle;
//   · at every hour mark with nothing due within the first lead, a `silence` row.
#pragma once
#include "casefile.h"
#include "closure.h"
#include "tape.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace caseclock {

struct ClockConfig {
    std::vector<int64_t> leads{60, 15, 0};   // descending; 0 = at the deadline
    int64_t max_lead() const;
    int64_t min_lead() const;
};

struct Bound {   // what the tape last derived for an event
    int64_t latest = INF, earliest = -INF;
    std::vector<std::string> chain;   // rendered constraints
};

struct Deadline {   // an open deadline, for the strip, --json and the sign-out
    std::string event;
    int64_t latest = 0, earliest = 0, slack = 0;
    std::vector<Constraint> chain;
    bool breached() const { return slack < 0; }
};

struct ClockState {   // the fold
    std::map<std::string, Bound> derived;
    std::map<std::string, std::set<std::string>> said;   // event → reasons said since its last derived row
    int64_t last_said_at = INT64_MIN;                    // minute of the last timer-driven line
    std::set<int64_t> silence_hours;
    std::string infeasible_key;                          // the cycle last said; empty when feasible
    std::vector<FactSpec> facts;                         // fact rows in order (the latest per event binds)
    std::map<std::string, int64_t> fact_seq;
    int64_t last_signout_seq = -1;
    int64_t last_rules_seq = -1;
    std::string last_rules_hash;
    int64_t rows_said = 0, rows_held = 0, rows_withdrawn = 0, rows_silence = 0, rows_fact = 0, rows_infeasible = 0;
};
void fold_step(ClockState& s, const Entry& e);
ClockState fold_clock(const Tape& tape);

std::string reason_text(const std::string& reason);   // "lead_60" → "due in 60 min" …

class CaseRuntime {
public:
    CaseDoc doc;
    std::vector<RulePack> packs;
    ClockConfig cfg;
    Tape tape;
    std::string tape_path;   // "" = in memory only
    bool encrypt = true;
    STN stn;
    Closure closure;
    ClockState state;
    int64_t now = 0;
    std::string rules_hash;
    std::string by;   // who is at the keyboard
    std::vector<std::string> log;   // what happened, one line per row written (the seam)
    bool persist = true;         // false: rows stay in memory (replays in tests)
    bool observe_only = false;   // true: read the tape and close; write no row at all (--json, --mcp, --spool)
    bool has_preload = false;    // true: `preload` is the tape, instead of the file at tape_path
    Tape preload;
    std::string clock_source;    // where `now` came from, for the status line

    bool load(const CaseDoc& d, const std::vector<RulePack>& p, const std::string& tape_file, bool encrypt_tape, int64_t now_minute, std::string* err);
    bool add_fact(const std::string& event, int64_t minutes, const std::string& who, int64_t at, std::string* err);
    void tick(int64_t now_minute);           // the minute changed

    // replay: the case's facts entered later than the start are scheduled, not loaded
    bool replaying = false;
    int64_t replay_start = 0, replay_end = 0;
    std::vector<FactSpec> scheduled;
    bool apply_scheduled(int64_t minute, std::string* err);   // the facts whose entered minute is `minute`
    bool record_signout(const std::string& text, const std::string& who, const std::string& next, std::string* err);
    bool flush(std::string* err);            // rows not yet on disk

    std::vector<std::string> watched() const;
    std::vector<Deadline> open() const;      // finite latest, not pinned by a fact; nearest first
    bool nearest(Deadline& out) const;
    bool deadline_for(const std::string& event, Deadline& out) const;
    bool infeasible() const { return !closure.consistent(); }
    std::vector<std::string> layers() const;   // present, sorted
    std::string layers_text() const;           // "layers L0 L1 L2 L3 L4 · packs none"
    std::string strip_text() const;            // the line the strip shows for this case
    std::string slack_text(int64_t slack) const;
    std::string explain(const std::string& event) const;
    Json to_json() const;                      // §8

    enum class Mood { Quiet, Amber, Red, Violet };
    Mood mood() const;

private:
    size_t persisted_ = 0;
    const Entry& row(const std::string& kind, int64_t at, Json body);
    const Entry& correction(int64_t supersedes, const std::string& kind, int64_t at, Json body);
    bool reclose(std::string* err);
    void derive(const Json& cause);            // derived / withdrawn / infeasible rows after a closure
    void speak(bool after_closure);            // the timer rules for `now`
    std::string line_for(const Deadline& d) const;
    std::string cycle_text(const std::vector<std::string>& cyc, int64_t short_by) const;
    std::string reason_for(int64_t slack) const;
};

int64_t floor_div(int64_t a, int64_t b);
int64_t floor_mod(int64_t a, int64_t b);

// The replay plan: the span the case's narration and facts cover, and the facts to enter along the
// way. `at_start` is the case with only the facts already entered by `start`.
struct ReplayPlan {
    int64_t start = 0, end = 0;
    CaseDoc at_start;
    std::vector<FactSpec> later;
};
ReplayPlan plan_replay(const CaseDoc& doc, bool has_end, int64_t end_override);

}  // namespace caseclock
