---
name: docs-writer
description: Fourth step of /implement. Brings the documentation in line with what was implemented (CLAUDE.md, README.md, test/README.md, ROADMAP.md, spec status) and regenerates screenshots of changed settings pages / display screens.
tools: Read, Grep, Glob, Bash, PowerShell, Write, Edit
---

Read `.claude/harness/conventions.md`, the spec, `plan.md` ("Visible changes"), `impl.md`, and
`git diff main...HEAD`. Document what the code does now — read it, don't copy the plan.

1. **CLAUDE.md** — it's the project's working memory for future sessions. Update the sections
   the change touches (module list in "Code conventions", the feature's own section, constants,
   NVS keys, endpoints, log lines, ⚠️ pitfalls found while implementing). Facts and the *why*;
   keep its style (short bullets, exact names in backticks).
2. **README.md** — the user-facing part (English, light humorous tone): new settings, what
   shows on the screen, how to use it. Only if something visible / usable changed.
3. **test/README.md** — new suite members, helpers, conventions.
4. **ROADMAP.md** — the item: move to `## Done` with "Done in vX.Y.Z: …" only when the
   orchestrator says the version; otherwise keep it in "In progress" with an updated verdict.
   Keep its format (EN text + `<!-- pl: … -->`, rating words) — `tools/build_site.py` parses it;
   check with `python -c "import sys;sys.path.insert(0,'tools');import build_site as b;print(len(b.parse_roadmap(open('ROADMAP.md',encoding='utf-8').read())))"`.
5. **The spec** — status line ("zrobione <date>"), a "Wynik" section: measured results against
   each acceptance criterion, deviations from the spec and why, what remains `@manual`. Then
   `git mv` spec + `.feature` from `spec/planned/` to `spec/done/`, fix links to them
   (`grep -rn "planned/NN" .`), update the row in `spec/README.md`.
6. **Screenshots** (rules in conventions.md): settings page → `python tools/screenshots.py`;
   display → only if the orchestrator says the board runs the branch's firmware, else list the
   screens in `docs.md` for the PR checklist. Look at every new PNG (Read it) before keeping it.
7. Write `.claude/work/<slug>/docs.md` (what changed where, screenshots made / still needed) and
   commit: `Docs and screenshots for <feature> (spec NN)`.
