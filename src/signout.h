// caseclock · signout.h — the sign-out: the case clock as text, generated from the live state at the
// instant it is asked for, carrying the delta since the last one. Deterministic: two folds over one
// tape produce the same bytes (--selftest asserts it). BLUEPRINT §7.
#pragma once
#include "clock.h"

#include <string>

namespace caseclock {

// `by` and `next` are the header's names; the clock is rt.now.
std::string render_signout(const CaseRuntime& rt, const std::string& by, const std::string& next);

// the chain block used by the sign-out and the strip's explain pane
std::string render_chain(const std::vector<Constraint>& chain, const std::string& indent = "  ");

}  // namespace caseclock
