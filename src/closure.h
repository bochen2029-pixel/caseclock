// caseclock · closure.h — the temporal closure, a bit-identical port of REGISTRAR floor/closure.py.
//
// A donor case is a Simple Temporal Network: events joined by constraints x_later − x_earlier ≤ w
// in whole minutes. Consistent iff the distance graph has no negative cycle; the tightest implied
// bounds are the all-pairs shortest paths in the (min, +) semiring; the feasible window of event j
// is [−D[j][0], D[0][j]]; the shortest path is the explanation. Integer arithmetic only.
//
// Every loop here runs in the reference's order with the reference's tie-breaking (strict <), so
// --selftest can assert equality with the Python outputs recorded in tests/expected/, never tolerance.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace caseclock {

constexpr int64_t INF = 0x3F3F3F3F;   // the reference's sentinel: INF + INF fits an int32; we keep int64 headroom
constexpr const char* REFERENCE = "T0";   // index 0: the case clock's origin

struct Constraint {
    std::string later, earlier;
    int64_t weight = 0;   // x_later - x_earlier <= weight
    std::string label, layer;   // L0 policy · L1 medicine · L2 site · L3 integration · L4 the record
    std::string source;         // caseclock addition: the citation (ignored by the reference)
    std::string render() const;   // "later - earlier <= w  [layer]  label" (rstripped), as the reference
};

struct Closure;

struct STN {
    std::vector<std::string> names{REFERENCE};
    std::vector<Constraint> constraints;

    int node(const std::string& name);                 // index, appending if new (first-seen order)
    int index_of(const std::string& name) const;       // -1 if absent
    void at_most(const std::string& later, const std::string& earlier, int64_t minutes, const std::string& label = "", const std::string& layer = "", const std::string& source = "");
    void at_least(const std::string& later, const std::string& earlier, int64_t minutes, const std::string& label = "", const std::string& layer = "", const std::string& source = "");
    void window(const std::string& event, int64_t opens, int64_t closes, const std::string& label = "", const std::string& layer = "", const std::string& source = "");
    void at(const std::string& event, int64_t minutes, const std::string& label = "", const std::string& layer = "", const std::string& source = "");
    Closure close() const;
};

struct Closure {
    std::vector<std::string> names;
    std::vector<Constraint> constraints;   // the STN's, same order
    int n = 0;
    std::vector<int64_t> D;      // n×n, row-major
    std::vector<int> nxt;        // n×n, -1 = None: first hop on the shortest path i → j
    std::map<std::pair<int, int>, size_t> origin;   // edge → index into constraints (the tightest; the first of equals wins)

    int idx(const std::string& name) const;   // -1 if absent
    bool has(const std::string& name) const { return idx(name) >= 0; }
    int64_t d(int i, int j) const { return D[(size_t)i * (size_t)n + (size_t)j]; }
    int next(int i, int j) const { return nxt[(size_t)i * (size_t)n + (size_t)j]; }

    bool consistent() const;
    std::vector<std::string> negative_cycle() const;   // empty = None; else closes on itself
    std::vector<Constraint> cycle_constraints(const std::vector<std::string>& cycle) const;

    int64_t latest(const std::string& event) const;     // D[0][e]
    int64_t earliest(const std::string& event) const;   // -D[e][0]
    int64_t slack(const std::string& event, int64_t now) const { return latest(event) - now; }

    std::vector<std::string> path(int i, int j) const;     // event names along the shortest path
    std::vector<Constraint> binding_path(const std::string& event) const;
    std::string explain(const std::string& event) const;   // the reference's text, verbatim
};

std::string hhmm(int64_t minutes);      // "HH:MM", "HH:MM (-1d)", "unbounded"
bool finite_bound(int64_t v);           // |v| < INF / 2, the same test hhmm uses
// "HH:MM", "HH:MM -1d", "HH:MM (-1d)", "HH:MM +2d", or plain minutes ("-105") → minutes; false if unparsable
bool parse_hhmm(const std::string& text, int64_t& out);

}  // namespace caseclock
