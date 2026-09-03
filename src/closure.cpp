// caseclock · closure.cpp — see closure.h. Ported line for line from REGISTRAR floor/closure.py.
#include "closure.h"

#include "app_util.h"

#include <cstdlib>

namespace caseclock {

std::string Constraint::render() const {
    const std::string tag = layer.empty() ? std::string() : "  [" + layer + "]";
    return rstrip(later + " - " + earlier + " <= " + std::to_string(weight) + tag + "  " + label);
}

// ── construction ────────────────────────────────────────────────────────────
int STN::node(const std::string& name) {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return (int)i;
    names.push_back(name);
    return (int)names.size() - 1;
}
int STN::index_of(const std::string& name) const {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return (int)i;
    return -1;
}
void STN::at_most(const std::string& later, const std::string& earlier, int64_t minutes, const std::string& label, const std::string& layer, const std::string& source) {
    node(later);
    node(earlier);
    constraints.push_back(Constraint{later, earlier, minutes, label, layer, source});
}
void STN::at_least(const std::string& later, const std::string& earlier, int64_t minutes, const std::string& label, const std::string& layer, const std::string& source) {
    // x_later - x_earlier >= minutes, i.e. x_earlier - x_later <= -minutes
    node(later);
    node(earlier);
    constraints.push_back(Constraint{earlier, later, -minutes, label, layer, source});
}
void STN::window(const std::string& event, int64_t opens, int64_t closes, const std::string& label, const std::string& layer, const std::string& source) {
    at_most(event, REFERENCE, closes, label + " closes", layer, source);
    at_least(event, REFERENCE, opens, label + " opens", layer, source);
}
void STN::at(const std::string& event, int64_t minutes, const std::string& label, const std::string& layer, const std::string& source) {
    // a completed event, pinned to a known time (load_case's "at")
    at_most(event, REFERENCE, minutes, label, layer, source);
    at_least(event, REFERENCE, minutes, label, layer, source);
}

// ── the closure ─────────────────────────────────────────────────────────────
Closure STN::close() const {
    Closure c;
    c.names = names;
    c.constraints = constraints;
    const int n = (int)names.size();
    c.n = n;
    const size_t N = (size_t)n;
    c.D.assign(N * N, INF);
    for (size_t i = 0; i < N; ++i) c.D[i * N + i] = 0;

    // Keep the tightest constraint per edge, and remember which one it was so a path can explain
    // itself. Strict <: the first of equals wins, as the reference.
    for (size_t k = 0; k < constraints.size(); ++k) {
        const Constraint& cs = constraints[k];
        const int i = index_of(cs.earlier), j = index_of(cs.later);
        if (cs.weight < c.D[(size_t)i * N + (size_t)j]) {
            c.D[(size_t)i * N + (size_t)j] = cs.weight;
            c.origin[{i, j}] = k;
        }
    }

    // nxt[i][j] = first hop on the shortest path i -> j
    c.nxt.assign(N * N, -1);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j && c.D[(size_t)i * N + (size_t)j] < INF) c.nxt[(size_t)i * N + (size_t)j] = j;

    // Floyd-Warshall in (min, +), in place, in the reference's exact order. Dik is captured once per
    // row (as the Python local is); Dk[j] and nxt[i][k] are read live (as the Python lists are).
    int64_t* D = c.D.data();
    int* nxt = c.nxt.data();
    for (size_t k = 0; k < N; ++k) {
        const int64_t* Dk = D + k * N;
        for (size_t i = 0; i < N; ++i) {
            const int64_t Dik = D[i * N + k];
            if (Dik >= INF) continue;
            int64_t* Di = D + i * N;
            int* nxti = nxt + i * N;
            for (size_t j = 0; j < N; ++j) {
                if (Dk[j] >= INF) continue;
                const int64_t cand = Dik + Dk[j];
                if (cand < Di[j]) {
                    Di[j] = cand;
                    nxti[j] = nxt[i * N + k];
                }
            }
        }
    }
    return c;
}

// ── consistency ─────────────────────────────────────────────────────────────
int Closure::idx(const std::string& name) const {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return (int)i;
    return -1;
}

bool Closure::consistent() const {
    for (int i = 0; i < n; ++i)
        if (d(i, i) < 0) return false;
    return true;
}

std::vector<std::string> Closure::negative_cycle() const {
    // A node with a negative self-distance may only REACH the cycle rather than lie on it — the
    // reference point usually does. Walk toward i and take the first node seen twice.
    for (int i = 0; i < n; ++i) {
        if (d(i, i) >= 0) continue;
        std::map<int, size_t> seen;
        std::vector<int> order;
        int cur = i;
        for (int step_no = 0; step_no < 2 * n + 2; ++step_no) {
            auto it = seen.find(cur);
            if (it != seen.end()) {
                std::vector<std::string> ring;
                for (size_t x = it->second; x < order.size(); ++x) ring.push_back(names[(size_t)order[x]]);
                ring.push_back(names[(size_t)cur]);
                return ring;
            }
            seen[cur] = order.size();
            order.push_back(cur);
            const int step = next(cur, i);
            if (step < 0) break;
            cur = step;
        }
    }
    return {};
}

std::vector<Constraint> Closure::cycle_constraints(const std::vector<std::string>& cycle) const {
    std::vector<Constraint> out;
    for (size_t x = 0; x + 1 < cycle.size(); ++x) {
        auto it = origin.find({idx(cycle[x]), idx(cycle[x + 1])});
        if (it != origin.end()) out.push_back(constraints[it->second]);
    }
    return out;
}

// ── windows ─────────────────────────────────────────────────────────────────
int64_t Closure::latest(const std::string& event) const { return d(0, idx(event)); }
int64_t Closure::earliest(const std::string& event) const { return -d(idx(event), 0); }

// ── the explanation ─────────────────────────────────────────────────────────
std::vector<std::string> Closure::path(int i, int j) const {
    if (next(i, j) < 0 && i != j) return {};
    std::vector<std::string> out{names[(size_t)i]};
    int cur = i, guard = 0;
    while (cur != j) {
        const int nx = next(cur, j);
        if (nx < 0 || guard > n * 2) break;
        cur = nx;
        out.push_back(names[(size_t)cur]);
        guard++;
    }
    return out;
}

std::vector<Constraint> Closure::binding_path(const std::string& event) const {
    // Not commentary added afterwards: the shortest path that realises the bound, recovered from
    // the same computation that produced it.
    const std::vector<std::string> hops = path(0, idx(event));
    std::vector<Constraint> out;
    for (size_t x = 0; x + 1 < hops.size(); ++x) {
        auto it = origin.find({idx(hops[x]), idx(hops[x + 1])});
        if (it != origin.end()) out.push_back(constraints[it->second]);
    }
    return out;
}

std::string Closure::explain(const std::string& event) const {
    std::string s = event + ": latest " + hhmm(latest(event)) + "\n  because \xE2\x80\x94";
    int64_t running = 0;
    for (const Constraint& c : binding_path(event)) {
        running += c.weight;
        s += "\n    " + pad_cp(c.render(), 62) + " cumulative " + hhmm(running);
    }
    return s;
}

// ── presentation ────────────────────────────────────────────────────────────
bool finite_bound(int64_t v) { return (v < 0 ? -v : v) < INF / 2; }

std::string hhmm(int64_t minutes) {
    if (!finite_bound(minutes)) return "unbounded";
    int64_t day = minutes / 1440, rem = minutes % 1440;   // Python divmod: the remainder is never negative
    if (rem < 0) { rem += 1440; day -= 1; }
    std::string s = ssprintf("%02lld:%02lld", (long long)(rem / 60), (long long)(rem % 60));
    if (day) s += ssprintf(" (%+lldd)", (long long)day);
    return s;
}

bool parse_hhmm(const std::string& text, int64_t& out) {
    std::string t = rstrip(text);
    size_t a = 0;
    while (a < t.size() && t[a] == ' ') ++a;
    t = t.substr(a);
    if (t.empty()) return false;
    // plain minutes
    {
        char* end = nullptr;
        const long long v = strtoll(t.c_str(), &end, 10);
        if (end && *end == 0 && end != t.c_str()) { out = v; return true; }
    }
    // HH:MM [ (±Nd) | ±Nd ]
    if (t.size() < 4) return false;
    char* end = nullptr;
    const long long hh = strtoll(t.c_str(), &end, 10);
    if (!end || *end != ':' || end == t.c_str()) return false;
    const char* p = end + 1;
    char* end2 = nullptr;
    const long long mm = strtoll(p, &end2, 10);
    if (!end2 || end2 == p || (end2 - p) != 2) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    std::string rest = end2;
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    long long day = 0;
    if (!rest.empty()) {
        if (rest.front() == '(' && rest.back() == ')') rest = rest.substr(1, rest.size() - 2);
        if (rest.empty() || rest.back() != 'd') return false;
        rest.pop_back();
        char* e3 = nullptr;
        day = strtoll(rest.c_str(), &e3, 10);
        if (!e3 || *e3 != 0 || e3 == rest.c_str()) return false;
    }
    out = day * 1440 + hh * 60 + mm;
    return true;
}

}  // namespace caseclock
