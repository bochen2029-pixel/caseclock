// caseclock · casefile.h — the case file and the rule packs: REGISTRAR's fixture JSON, verbatim,
// plus caseclock's optional additions (facts, replay, expected, verified_by/on, source, reference_at).
// Rules are data, layered, owned: L0/L1 ship as packs once they carry citations; L2/L3 live in the
// site's git; L4 is the record (the facts). The repository ships no pack, only synthetic cases.
#pragma once
#include "closure.h"
#include "json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace caseclock {

struct ConstraintSpec {
    std::string kind;   // at_least | at_most | window | at
    std::string event, later, earlier;
    int64_t minutes = 0, opens = 0, closes = 0;
    std::string label, layer, source;
};

struct FactSpec {
    std::string event;
    int64_t minutes = 0;   // when the event happened, minutes from the reference
    std::string by;        // the coordinator's name
    std::string entered_at;   // wall clock text as written ("14:02 (-1d)"); parsed when it is hhmm
    bool has_entered = false;
    int64_t entered = 0;   // minutes from the reference when it was entered
    std::string label;     // optional
};

struct ReplayLine {
    int64_t at = 0;
    std::string say;
};

struct CaseDoc {
    std::string path;   // as given, UTF-8
    std::string id;
    bool synthetic = false;
    std::string note, reference;
    bool has_now = false;
    int64_t now = 0;
    std::string reference_at;   // caseclock: local "YYYY-MM-DDTHH:MM" of minute 0 (optional)
    std::vector<std::string> watch, explain;
    std::string verified_by, verified_on;
    std::vector<ConstraintSpec> constraints;
    std::vector<FactSpec> facts;
    std::vector<ReplayLine> replay;
    Json expected;   // the fixture's expected block, for --selftest
    Json raw;
    std::string sha256;   // of the file's bytes
};

struct RulePack {
    std::string path, name, layer, sha256;
    bool has_leads = false;
    std::vector<int64_t> lead_minutes;
    std::vector<ConstraintSpec> constraints;
    std::string verified_by, verified_on;
};

bool parse_constraint(const Json& j, ConstraintSpec& out, std::string* err);
bool load_case_file(const std::string& path, CaseDoc& out, std::string* err);
bool load_case_text(const std::string& text, const std::string& path, CaseDoc& out, std::string* err);
bool load_rule_pack(const std::string& path, RulePack& out, std::string* err);

// Add one constraint as load_case does; false on an unknown kind.
bool add_constraint(STN& stn, const ConstraintSpec& c, std::string* err);
// The network: packs in order, then the case's constraints, then the facts as L4 "at" constraints.
// The reference's load_case is build_stn({}, doc, {}).
bool build_stn(const std::vector<RulePack>& packs, const CaseDoc& doc, const std::vector<FactSpec>& facts, STN& out, std::string* err);

// The reference's report(path, now) text, byte for byte; returns its exit code (1 = infeasible).
int report_text(const CaseDoc& doc, const STN& stn, const Closure& c, bool has_now, int64_t now, std::string& out);

}  // namespace caseclock
