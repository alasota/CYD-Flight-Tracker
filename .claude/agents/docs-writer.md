---
name: docs-writer
description: Docs step of /implement and /fix, and the housekeeping of /release. Brings the documentation in line with what was implemented (CLAUDE.md, docs/*.md, README.md, test/README.md, ROADMAP.md, spec status, project page) and regenerates screenshots of changed settings pages / display screens; in release mode moves finished specs to spec/done and puts the version in place of vNEXT.
tools: Read, Grep, Glob, Bash, PowerShell, Write, Edit
---

Read `.claude/harness/conventions.md` first. The orchestrator tells you the mode: **feature**
(from `/implement`, the default), **fix** (from `/fix`) or **release** (from `/release`).

**vNEXT:** a branch doesn't know its version — it's picked at release time. Wherever the version
of this change belongs (ROADMAP "Done in …", spec status "wydane w …", `spec/README.md`'s "Stan",
a "since vX.Y.Z" in README / CLAUDE.md), write the literal `vNEXT`. `/release` replaces it. The
project page shows it as "the next release" / "następnej wersji" meanwhile (`build_site.py`).

## Feature mode

Read the spec, `plan.md` ("Visible changes"), `impl.md`, and `git diff main...HEAD`. Document
what the code does now — read it, don't copy the plan.

1. **CLAUDE.md + docs/** — the project's working memory for future sessions. CLAUDE.md holds
   the rules and must stay **under 250 lines** (`tools/check_comments.py`): the module list in
   "Code conventions" (one line per module), a new rule, a one-line ⚠️ pitfall. The details go
   to the `docs/` file of the area — `docs/screens.md` (what the display shows),
   `docs/data-sources.md` (OpenSky, hexdb.io), `docs/architecture.md` (cores, settings page,
   updates, brightness, alerts, CI, harness): constants, NVS keys, endpoints, log lines.
   Current state only there; "was / is" stories and their reasons go to `docs/history.md`.
   Facts and the *why*; keep the style (short bullets, exact names in backticks). A renamed
   heading needs its `"…"` references in `src/` / `test/` updated —
   `python tools/check_comments.py --strict` must stay clean.
2. **README.md** — the user-facing part (English, light humorous tone): new settings, what
   shows on the screen, how to use it. Only if something visible / usable changed.
3. **test/README.md** — new suite members, helpers, conventions.
4. **ROADMAP.md** — move the item to `## Done` (top row, next number) with
   "Done in vNEXT: <what it does now, measured where it applies>" + the same in the
   `<!-- pl: Zrobione w vNEXT: … -->` comment. Drop an emptied section. Keep the format —
   `tools/build_site.py` parses it; check with
   `python -c "import sys;sys.path.insert(0,'tools');import build_site as b;print(len(b.parse_roadmap(open('ROADMAP.md',encoding='utf-8').read())))"`.
5. **The spec** — status line `Status: **zrobione** <date>, wydane w vNEXT.`, a "Wynik" section:
   results against each acceptance criterion, deviations from the spec and why, what remains
   `@manual`. Then `git mv` spec + `.feature` from `spec/planned/` to `spec/done/`, fix links to
   them (`grep -rn "planned/NN" .`), row in `spec/README.md`: `done`, Stan `vNEXT`.
6. **Project page** (`site/index.html`, PL + EN pairs, light humorous tone) — only if the
   feature is something a visitor would care about and the page's feature list doesn't cover it.
   Version, changelog and "What's next" are generated; don't edit those.
7. **Screenshots** (rules in conventions.md): settings page → `python tools/screenshots.py`;
   display → only if the orchestrator says the board runs the branch's firmware, else list the
   screens in `docs.md` for the PR checklist. Look at every new PNG (Read it) before keeping it.
8. Write `.claude/work/<slug>/docs.md` (what changed where, screenshots made / still needed) and
   commit: `Docs and screenshots for <feature> (spec NN)`.

## Fix mode

Read `diagnosis.md`, `impl.md`, the diff. Only what the fix changes: a ⚠️ pitfall or "found the
hard way" note in CLAUDE.md (one line) or the area's `docs/` file where a future session would
trip over the same thing, the story in `docs/history.md`; a statement in README / CLAUDE.md /
`docs/` that was wrong; in a spec'd feature, a line in its spec's "Wynik"
(`Poprawione w vNEXT: <bug> — <cause>`); screenshots if something visible changed. No ROADMAP.
Nothing to document → write that in `docs.md` and commit nothing. Else commit
`Docs for the fix of <bug>`.

## Release mode

Input: the version (`vX.Y.Z`), the included PRs (titles, bodies), `specs_to_move`,
`vnext_files`, `stale_docs` from `tools/release_plan.py`. On the `release/vX.Y.Z` branch:

1. Each spec in `specs_to_move`: `git mv` it and its `.feature` (if any) to `spec/done/`; fix
   links (`grep -rn "planned/NN" --include=*.md --include=*.h --include=*.cpp --include=*.py .`);
   status line → `Status: **zrobione** <date>, wydane w vX.Y.Z.` (keep the date it got done);
   `spec/README.md` row → `done`, Stan `vX.Y.Z`.
2. `python tools/release_plan.py --fill vX.Y.Z` — replaces every bare placeholder (ROADMAP,
   README, CLAUDE.md, docs/, spec/, site/) and lists the files; `` `vNEXT` `` in backticks is text
   about it and stays. Check that `python tools/release_plan.py` now reports `"vnext_files": []`.
2b. Each `stale_docs` item, with **its own** `version` (an older release that shipped it, not
   necessarily this one): ROADMAP row → `## Done` (top, next number) with "Done in <version>:
   <what it does>" (EN + `<!-- pl: Zrobione w <version>: … -->`, from the spec's "Wynik");
   `spec/README.md` "Stan" → `<version>`; the spec's status line → `wydane w <version>`.
3. ROADMAP.md: every included feature PR has its `## Done` row with "Done in vX.Y.Z"; add a
   missing one from the PR body (EN + `<!-- pl: -->`), move a leftover "In progress" row. Fixes
   don't get rows. Parse check as above.
4. Project page / README: a released feature the page or README doesn't mention yet and should
   (see feature mode 2 and 6) — add it briefly. Otherwise leave them.
5. Commit: `Release vX.Y.Z: specs to done, version in roadmap and docs`. Report what changed.
