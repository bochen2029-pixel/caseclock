# floor/cases — synthetic cases, zero PHI, forever

Every file here is synthetic: not de-identified, not sampled, not perturbed. Real case data is
inadmissible in this repository for any purpose. Every duration is illustrative and none is a
clinical or regulatory claim; if you find yourself quoting a number from this directory, stop.

Format: REGISTRAR's fixture format (`kind` = `at_least` | `at_most` | `window` | `at`, with
`label` and `layer`), plus caseclock's optional `facts`, `replay`, `expected`, `verified_by`,
`verified_on`. The `expected` block is what `--selftest` asserts, by equality, against the
closure; the values were hand-derived from the constraints and match REGISTRAR's `closure.py`.

`tr-4118.synthetic.json` is the case on the opnaorta.ai fold: the serology deadline that was
22:15 the previous evening, derived from seven ordinary constraints entered nowhere.
`infeasible-transport.synthetic.json` is the plan that cannot be met while every timer is green.
