# Brief for Claude Design — opnaorta.ai/tools, adding tools 03 and 04

Paste everything below the line into Claude Design, with `C:\Websites\aorta-site\_upload\tools.html`
attached or open as the page to extend. The captures it can use: `C:\facet\docs\screenshot-in-use.png`
(facet, already on the page), `C:\GPUz\` for vramtop's capture (already on the page); for everywho and
caseclock there is no capture yet, so the brief describes the mock to draw.

---

You are extending the existing page `tools.html` (opnaorta.ai/tools), not redesigning it. Keep every
existing section, token, font and rule exactly as they are: the header, the badge line "FREE · MIT ·
WINDOWS · FOR HUMANS AND THEIR AGENTS", the hero "Small tools. One job each. Free.", the two shipped
tool sections (facet, vramtop) with their READING THE SCREEN and FOR THE AGENT blocks, section 04 HOUSE
RULES with its six rules verbatim, section 05 THE SHOP IS OPEN, and the DWG-001 drawing. Add two tools
in the same visual grammar and update the counters honestly. No new colours, no new fonts, no
marketing adjectives, no performance numbers that are not on this page already.

## 1. The catalog table (section 01)

Add two rows after vramtop, same columns (TOOL · DOES · HANDS AN AGENT · PLATFORM · STATUS). Keep the
"next" row after them, reworded as below. Status legend: SHIPPED (green dot) · BUILDING (amber dot) ·
NEXT (grey dot). Add the legend once under the table.

Row 3 — **everywho** · BUILDING
- DOES: "Who is touching what, right now. Per-process, per-directory file and disk I/O from the
  kernel's own accounting — which agent is filling the disk, what the 03:00 job is touching, who has
  the file you cannot delete — as a live rail you drill into."
- HANDS AN AGENT: "The process, the directory and the bytes per second, as one JSON line; a tape of
  the files touched in the last N seconds that facet and everywhere read directly."
- PLATFORM: Windows

Row 4 — **caseclock** · NEXT
- DOES: "The case clock. Every deadline a donor case implies, derived from the facts already
  entered, counted down on a strip over the record, and written to a tape with the chain of
  constraints that produced it."
- HANDS AN AGENT: "The next deadline, its chain, and the minutes left, as one line; `--json` for all
  of them; a read-only MCP with three verbs."
- PLATFORM: Windows

"next" row: "The next tool follows the same house rules — one job, free, keyboard-first, honest about
what it measures." (unchanged) — keep it as the last row.

## 2. Stats strip under the hero

Change "2 TOOLS SHIPPED" to "2 SHIPPED · 2 IN THE SHOP". Keep "108 ms · A 316-HIT SEARCH", "27 ·
PROCESSES ON ONE GPU", "$0 · EVERY TOOL". Do not invent a number for the new tools; when everywho
ships, its number will be "N processes touching the disk in a 3-second sample" and caseclock's will be
"7 · CONSTRAINTS IN THE CHAIN" — leave placeholders out entirely until then.

## 3. Section — TOOL 03 · BUILDING: everywho

Title: **everywho — who is touching what, right now.**

Paragraph: "Task Manager shows the disk at 100 %. facet shows what landed on it last night. everywho
shows who is writing which directory this second — every process, every folder, from the kernel's
own I/O accounting — so the question "which agent is filling the disk" has a name attached. Two tiers:
without elevation it names who and how much; elevated, it names what, file by file, and never reads a
byte of any of them."

Badges: WINDOWS · FREE · MIT · KEYBOARD-FIRST · PINS ON TOP · NO AI INSIDE · TWO TIERS

Button: "everywho on GitHub" — grey, labelled "when --selftest is green" (it is not shipped; do not
style it as live).

Capture to draw (mock, labelled MOCK in the corner like the fold's REPLAY): the same window family as
facet — a left rail with sections WHO (process rows with a share bar: node.exe ×3 41 MB, Everything.exe
3.1 MB, chrome.exe ×14 2.2 MB, System 1.9 MB), WHERE (a directory tree with bytes), OPERATIONS (read /
write / create / delete counts), VOLUMES (C: write 48 MB/s, busy 61 %, a small sparkline); a right table
with columns Process · Path · Write · Read · Ops; a top band per volume with a 60-second sparkline; a
status line reading verbatim: `everywho · etw (elevated) · 12,406 events · 0 lost · P pause · F freeze ·
Ctrl+L filter · Esc clear · Ctrl+T pin`.

READING THE SCREEN (four items, same style as facet's):
01 "The band is the disk. Per volume: file MB/s and disk MB/s side by side — a cached write is a file
write now and a disk write later, and the tool shows both rather than pretending they are one."
02 "WHO is the rail. Every process with I/O in the window, share bars by bytes; agent sessions carry
their harness, project and session id when the tool can attribute them, labelled with the rule that
did."
03 "Only, or not. Click a process or a folder to keep only it; right-click to exclude it; picks become
chips and the status line shows the filter as text."
04 "Lost events are printed. If the kernel dropped anything, the status line says so and the numbers
are lower bounds — never hidden."

Keys line: `P pause` · `F freeze` · `Ctrl+L filter` · `Esc clear` · `Ctrl+T pin`

FOR THE AGENT: "An agent that sees the disk pegged retries or waits blind. everywho -j names the
process and the directory in one line, and everywho --paths hands facet the exact files touched in
the last ten seconds — the same tape, no translation."

## 4. Section — TOOL 04 · NEXT: caseclock

Title: **caseclock — every deadline the case implies, on a strip over the record.**

Paragraph: "A donor case is a case under a clock, and the rules are all known pairwise: the lab
needs six hours, the team needs two, the OR opens at six. Nobody computes the consequence, so the
deadline that kills the morning OR window was 22:15 the previous evening and it was written nowhere.
caseclock computes it — a temporal closure over the facts already entered, exact, in whole minutes —
shows the nearest one on a 28-pixel strip pinned over the record, speaks at lead times by rule, and
prints the sign-out at handoff from the live state at that instant. No model. No network stack: the
exe cannot reach the internet, and the build proves it."

Badges: WINDOWS · FREE · MIT · KEYBOARD-FIRST · PINS ON TOP · NO AI INSIDE · NO NETWORK STACK · SYNTHETIC
DEMO, NO PHI

Button: "caseclock on GitHub" — grey, "blueprint; nothing built yet".

Capture to draw (two panels, both labelled MOCK · SYNTHETIC):
Panel A, the strip: a thin dark bar across the bottom of a blurred, unreadable record window, reading
verbatim `▍ TR-4118 · serology_drawn by 22:15 (−1d) · BREACHED 85 min · chain 7 · 3 open` with the
left mark in red, and a second smaller line `Ctrl+L fact · Ctrl+E explain · Ctrl+S sign-out · Ctrl+O
case · Ctrl+T pin · Esc`.
Panel B, the sign-out: a one-page monospace document titled `CASECLOCK · SIGN-OUT · TR-4118 · 23:40
(−1d)`, with sections DUE, IN ORDER (four rows: serology_drawn 22:15 (−1d) BREACHED 85 min chain 7;
match_run 04:15 slack 275 min; primary_acceptance 07:15; incision 09:15), SINCE THE LAST SIGN-OUT (three
rows: a fact, a said, a held ×38), and THE CHAIN FOR serology_drawn (seven constraint lines each with a
layer tag L0–L3 and a cumulative time ending at 22:15 (−1d)), and a footer `SYNTHETIC CASE · NO PHI ·
every duration illustrative · signed: ______`.

READING THE SCREEN:
01 "The strip is the whole interface. One line: the case, the nearest deadline, the minutes left,
the length of the chain. It pins over the record and never wants the screen."
02 "Derived, not entered. The 22:15 was never typed by anyone; it is the shortest path through seven
ordinary constraints. Ctrl+E shows the chain — the same computation that produced the number, so a
clinician checks it in seconds."
03 "Silence on the record. Every hour with nothing due is a row on the tape, and so is every line
the strip spoke. The sign-out lists both."
04 "Nothing leaves. No network stack in the binary, the tape encrypted per user, the sign-out on paper
when a human prints it. A human signs."

Keys line: `Ctrl+L fact` · `Ctrl+E explain` · `Ctrl+S sign-out` · `Ctrl+O case` · `Ctrl+T pin` · `Esc`

FOR THE AGENT: "caseclock --json is every open deadline with its chain and its slack; the read-only MCP
answers case_clock, case_explain and case_signout; no tool can enter a fact — that is a human's
keystroke."

Under the section, a one-line note in the page's small mono style: "caseclock is the free floor. VIGIL,
on the products page, is the fitted resident the practice installs on top of it — the same tape, plus
the mind that decides when."

## 5. DWG-001

In the drawing's "KEPT FAST BY / THE TOOLS" line, change "facet · vramtop · next" to "facet · vramtop ·
everywho · caseclock". Keep everything else.

## 6. Section 05

Change "Two shipped." to "Two shipped, two in the shop." Keep the request line.

## 7. Do not

Do not restyle facet or vramtop. Do not add testimonials, logos, or ratings. Do not use "AI-powered"
anywhere. Do not show a real record, a real name, or a real date in any mock; every mock carries
MOCK · SYNTHETIC in its corner the way the fold carries REPLAY. Do not present everywho or caseclock as
shipped.
