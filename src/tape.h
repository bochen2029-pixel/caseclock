// caseclock · tape.h — the case record: append-only, hash-chained, in REGISTRAR core/tape.py's format.
//
// There is no delete and no update in this interface. A correction is a new row that supersedes an
// older one; the older one stays readable forever. Every view is a fold over the rows.
//
// Row: {"seq","kind","at","body","prev","digest"}, canonical JSON (json.h), one per line.
// digest = BLAKE2b-256(prev · "\0" · canonical({"seq","kind","at","body"})), hex; the first prev is
// 64 zeros. A tape written here verifies under the reference, and vice versa (--selftest asserts it).
//
// At rest the file is either the reference's clear JSONL (header line {"case_id":…} then rows) or,
// by default, the same lines each DPAPI-protected for the Windows user and base64'd, one per line,
// under a first line "caseclock-tape 1 dpapi" — so an append is still an append.
#pragma once
#include "json.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace caseclock {

constexpr const char* GENESIS = "0000000000000000000000000000000000000000000000000000000000000000";
constexpr const char* kTapeMagic = "caseclock-tape 1 dpapi";

struct Entry {
    int64_t seq = 0;
    std::string kind;
    int64_t at = 0;   // whole minutes from the case reference
    Json body;
    std::string prev, digest;
    std::string to_json() const;   // the canonical row, no newline
    static std::string payload(int64_t seq, const std::string& kind, int64_t at, const Json& body);
};

std::string tape_digest(const std::string& prev, const std::string& payload);

class Tape {
public:
    std::string case_id;

    Tape() = default;
    explicit Tape(std::string id) : case_id(std::move(id)) {}

    // the only arrow in
    const Entry& append(const std::string& kind, int64_t at, Json body = Json::object());
    const Entry& correct(int64_t supersedes_seq, const std::string& kind, int64_t at, Json body = Json::object());

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const Entry& operator[](size_t i) const { return entries_[i]; }
    const std::vector<Entry>& entries() const { return entries_; }
    std::string head() const { return entries_.empty() ? GENESIS : entries_.back().digest; }
    std::set<int64_t> superseded() const;
    std::vector<const Entry*> current() const;   // rows no later row has superseded

    bool verify(std::string* err = nullptr) const;   // walk the chain; false at the first bad row
    bool intact() const { return verify(nullptr); }

    std::string to_jsonl() const;   // the rows
    std::string to_text() const;    // header + rows: the reference's save()
    static bool from_text(const std::string& text, Tape& out, std::string* err = nullptr);   // parse + verify

    // storage (sys.h underneath)
    static bool load_file(const std::string& path, Tape& out, bool* encrypted = nullptr, std::string* err = nullptr);
    bool save_file(const std::string& path, bool encrypt, std::string* err = nullptr) const;
    // append rows [from_seq, size) to the file, creating it with the header when absent
    bool append_file(const std::string& path, bool encrypt, size_t from_seq, std::string* err = nullptr) const;

private:
    std::vector<Entry> entries_;
    std::string line_for_file(const std::string& clear, bool encrypt, std::string* err) const;
};

std::string default_tape_dir();   // %LOCALAPPDATA%\caseclock (created)
std::string tape_path_for(const std::string& case_id, const std::string& dir = "");

}  // namespace caseclock
