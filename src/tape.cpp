// caseclock · tape.cpp — see tape.h.
#include "tape.h"

#include "app_util.h"
#include "hash.h"
#include "sys.h"

namespace caseclock {

std::string Entry::payload(int64_t seq, const std::string& kind, int64_t at, const Json& body) {
    Json j = Json::object();
    j.set("seq", Json::integer(seq));
    j.set("kind", Json::string(kind));
    j.set("at", Json::integer(at));
    j.set("body", body);
    return j.canonical();
}
std::string Entry::to_json() const {
    Json j = Json::object();
    j.set("seq", Json::integer(seq));
    j.set("kind", Json::string(kind));
    j.set("at", Json::integer(at));
    j.set("body", body);
    j.set("prev", Json::string(prev));
    j.set("digest", Json::string(digest));
    return j.canonical();
}

std::string tape_digest(const std::string& prev, const std::string& payload) {
    Blake2b256 h;
    h.update(prev.data(), prev.size());
    h.update("\0", 1);
    h.update(payload.data(), payload.size());
    uint8_t out[32];
    h.final(out);
    return to_hex(out, 32);
}

const Entry& Tape::append(const std::string& kind, int64_t at, Json body) {
    if (body.type != Json::Object) body = Json::object();
    Entry e;
    e.seq = (int64_t)entries_.size();
    e.kind = kind;
    e.at = at;
    e.body = std::move(body);
    e.prev = head();
    e.digest = tape_digest(e.prev, Entry::payload(e.seq, e.kind, e.at, e.body));
    entries_.push_back(std::move(e));
    return entries_.back();
}
const Entry& Tape::correct(int64_t supersedes_seq, const std::string& kind, int64_t at, Json body) {
    // A correction is an APPEND: the superseded row stays on the tape, readable forever.
    if (body.type != Json::Object) body = Json::object();
    body.set("supersedes", Json::integer(supersedes_seq));
    return append(kind, at, std::move(body));
}

std::set<int64_t> Tape::superseded() const {
    std::set<int64_t> dead;
    for (const Entry& e : entries_)
        if (const Json* s = e.body.get("supersedes"))
            if (s->is_int()) dead.insert(s->i);
    return dead;
}
std::vector<const Entry*> Tape::current() const {
    const std::set<int64_t> dead = superseded();
    std::vector<const Entry*> out;
    for (const Entry& e : entries_)
        if (!dead.count(e.seq)) out.push_back(&e);
    return out;
}

bool Tape::verify(std::string* err) const {
    std::string prev = GENESIS;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (e.seq != (int64_t)i) { if (err) *err = ssprintf("entry %zu: seq is %lld; entries are missing or reordered", i, (long long)e.seq); return false; }
        if (e.prev != prev) { if (err) *err = ssprintf("entry %zu (%s): prev digest does not match entry %zu", i, e.kind.c_str(), i ? i - 1 : 0); return false; }
        const std::string want = tape_digest(e.prev, Entry::payload(e.seq, e.kind, e.at, e.body));
        if (e.digest != want) { if (err) *err = ssprintf("entry %zu (%s): body has been altered since it was written", i, e.kind.c_str()); return false; }
        prev = e.digest;
    }
    return true;
}

std::string Tape::to_jsonl() const {
    std::string s;
    for (const Entry& e : entries_) { s += e.to_json(); s += '\n'; }
    return s;
}
std::string Tape::to_text() const {
    Json h = Json::object();
    h.set("case_id", Json::string(case_id));
    return h.canonical() + "\n" + to_jsonl();
}

bool Tape::from_text(const std::string& text, Tape& out, std::string* err) {
    out = Tape();
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= text.size()) {
        size_t q = text.find('\n', p);
        if (q == std::string::npos) q = text.size();
        std::string ln = text.substr(p, q - p);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (!rstrip(ln).empty()) lines.push_back(ln);
        p = q + 1;
    }
    if (lines.empty()) { if (err) *err = "empty tape"; return false; }
    Json hdr;
    std::string jerr;
    if (!json_parse(lines[0], hdr, &jerr) || !hdr.is_obj() || !hdr.get("case_id")) {
        if (err) *err = "tape header: " + (jerr.empty() ? std::string("not {\"case_id\":...}") : jerr);
        return false;
    }
    out.case_id = hdr.str("case_id");
    for (size_t i = 1; i < lines.size(); ++i) {
        Json row;
        if (!json_parse(lines[i], row, &jerr) || !row.is_obj()) { if (err) *err = ssprintf("tape line %zu: %s", i + 1, jerr.c_str()); return false; }
        const Json *seq = row.get("seq"), *kind = row.get("kind"), *at = row.get("at"), *body = row.get("body"), *prev = row.get("prev"), *dig = row.get("digest");
        if (!seq || !seq->is_int() || !kind || !kind->is_str() || !at || !at->is_int() || !body || !body->is_obj() || !prev || !prev->is_str() || !dig || !dig->is_str()) {
            if (err) *err = ssprintf("tape line %zu: not a tape row", i + 1);
            return false;
        }
        Entry e;
        e.seq = seq->i; e.kind = kind->s; e.at = at->i; e.body = *body; e.prev = prev->s; e.digest = dig->s;
        out.entries_.push_back(std::move(e));
    }
    return out.verify(err);
}

// ── storage ─────────────────────────────────────────────────────────────────
std::string Tape::line_for_file(const std::string& clear, bool encrypt, std::string* err) const {
    if (!encrypt) return clear + "\n";
    std::string b64;
    if (!dpapi_protect(clear, b64, err)) return "";
    return b64 + "\n";
}

bool Tape::load_file(const std::string& path, Tape& out, bool* encrypted, std::string* err) {
    std::string text;
    if (!read_file(path, text, err)) return false;
    const bool enc = starts_with(text, kTapeMagic);
    if (encrypted) *encrypted = enc;
    if (!enc) return from_text(text, out, err);
    std::string clear;
    size_t p = text.find('\n');
    if (p == std::string::npos) { if (err) *err = "encrypted tape without rows"; return false; }
    p += 1;
    size_t n = 0;
    while (p < text.size()) {
        size_t q = text.find('\n', p);
        if (q == std::string::npos) q = text.size();
        std::string ln = text.substr(p, q - p);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        p = q + 1;
        if (rstrip(ln).empty()) continue;
        std::string row;
        if (!dpapi_unprotect(ln, row, err)) { if (err) *err = ssprintf("tape line %zu: ", n + 2) + *err; return false; }
        clear += row;
        clear += '\n';
        ++n;
    }
    return from_text(clear, out, err);
}

bool Tape::save_file(const std::string& path, bool encrypt, std::string* err) const {
    make_dirs(dir_name(path));
    std::string data;
    if (encrypt) { data += kTapeMagic; data += '\n'; }
    Json h = Json::object();
    h.set("case_id", Json::string(case_id));
    std::string ln = line_for_file(h.canonical(), encrypt, err);
    if (ln.empty()) return false;
    data += ln;
    for (const Entry& e : entries_) {
        ln = line_for_file(e.to_json(), encrypt, err);
        if (ln.empty()) return false;
        data += ln;
    }
    return write_file(path, data, err);
}

bool Tape::append_file(const std::string& path, bool encrypt, size_t from_seq, std::string* err) const {
    if (!file_exists(path)) return save_file(path, encrypt, err);
    std::string data;
    for (size_t i = from_seq; i < entries_.size(); ++i) {
        const std::string ln = line_for_file(entries_[i].to_json(), encrypt, err);
        if (ln.empty()) return false;
        data += ln;
    }
    if (data.empty()) return true;
    return caseclock::append_file(path, data, err);
}

std::string default_tape_dir() {
    std::string d = path_join(local_app_data().empty() ? temp_dir() : local_app_data(), "caseclock");
    make_dirs(d);
    return d;
}
std::string tape_path_for(const std::string& case_id, const std::string& dir) {
    return path_join(dir.empty() ? default_tape_dir() : dir, sanitize_file_name(case_id) + ".tape.jsonl");
}

}  // namespace caseclock
