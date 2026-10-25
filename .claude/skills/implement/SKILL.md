---
name: implement
description: Implement one spec (spec/planned/NN-*.md) or one GitHub issue end to end on its own branch — plan + BDD scenarios (stop for the owner's approval), failing tests first, implementation, docs and screenshots, independent review, then a pull request whose CI builds a -dev release. Use when the user says "/implement 05", "/implement spec/planned/05-…md", "/implement #12" or "zaimplementuj issue 12".
argument-hint: "<spec number | spec path | #issue | issue URL>"
---

# /implement — spec or issue → branch → PR

You are the **orchestrator**. You don't write the code yourself: you prepare the run, spawn the
project's agents (`.claude/agents/`) one after another with the Agent tool, check each one's
result, talk to the owner, commit what agents leave uncommitted and open the PR. Shared rules for
everyone: `.claude/harness/conventions.md` — read it now.

Keep the owner informed with one short line per phase ("plan ready", "tests red: 7", …).

## 0. Preconditions

1. `git status --short`: unrelated uncommitted changes are fine (agents stage by name), but if
   files the spec touches are modified, ask the owner first.
2. `git fetch origin`. The branch starts from `origin/main`.
3. `gh auth status` — if `gh` is missing or not logged in, say once that the PR will need a
   manual click at the end (`winget install GitHub.cli` + `gh auth login` fixes that) and go on.

## 1. Resolve the input

- A number `05` or a path → `spec/planned/05-*.md` (must exist and not be in `spec/done/`).
  slug = `NN-<name>`, branch = `spec/NN-<name>`.
- `#12` / an issue URL → fetch it: `gh issue view 12 --json number,title,body,labels,url`, or
  without gh `curl -s https://api.github.com/repos/alasota/CYD-Flight-Tracker/issues/12`.
  slug = `issue-12-<kebab-title>`, branch = `issue/12-<kebab-title>`. The analyst turns it into
  a spec file.
- If the branch already exists (a resumed run), check it out and read `.claude/work/<slug>/` to
  see which phase finished; continue from there instead of starting over.
- Otherwise: `git switch -c <branch> origin/main`, `mkdir -p .claude/work/<slug>`.

## 2. Plan — agent `spec-analyst`

Spawn it with: slug, work folder, spec path or the fetched issue (title, body, url, labels).
When it returns, check `plan.md` and the `.feature` yourself: every acceptance criterion maps to
a scenario, `@manual` only where native can't work, steps are small.

Then **stop and show the owner**: the summary, the steps, the scenarios (title + how verified),
the visible changes (screens, settings tabs), the open questions. Ask with AskUserQuestion
whether to go ahead (options: go / change something). Don't continue without a yes. Feedback →
send it back to `spec-analyst` (SendMessage to the same agent, or a new one with the feedback)
and show the result again.

After the yes: commit the spec (if new), the `.feature`, the `spec/README.md` row and set the
spec's status line to `**w trakcie**`: `Plan and BDD scenarios for <feature> (spec NN)`.

## 3. Tests first — agent `test-author`

Check its result: `tests.md` exists, `python tools/check_bdd.py` is clean, the new tests fail
on assertions (not on the build), old tests still pass, the commit is there.

## 4. Implementation — agent `implementer`

Then verify yourself (don't trust the report): `pio test -e native` all green,
`pio run -e cyd` builds, `python tools/check_bdd.py` clean, `grep -rn "TODO(spec" src test`
empty. If not, send the implementer back with the failing output.

If the plan's visible changes include **display screens** and the board answers
(`curl -s -m 5 http://cyd-sky.local/perf.json`), ask the owner whether to flash the branch's
firmware for screenshots (`pio run -e cyd-ota -t upload`, password from `$CYD_WEB_PASSWORD`).
Only flash after a yes. Tell the docs-writer whether the board runs the branch.

## 5. Docs and screenshots — agent `docs-writer`

Feature mode. The version isn't known yet: it writes `vNEXT` (ROADMAP "Done in vNEXT", spec
status, `spec/README.md`), which `/release` replaces.
Look at the screenshots it made (Read the PNGs).

## 6. Review — agent `reviewer`

Blocking findings → `implementer` with `review.md` (then docs-writer again if docs are
affected) → `reviewer` again. At most **two** fix rounds; after that, list what's still open
for the owner and ask whether to open the PR anyway (as draft) or stop.

## 7. Pull request

1. Final checks once more (native tests, firmware build, check_bdd). Everything committed?
   `git status --short` shows nothing of yours.
2. `git push -u origin <branch>`.
3. Body from `.github/pull_request_template.md`, filled in — not left as a template:
   - What and why (for the person at the display), spec link, `Closes #N` for an issue.
   - BDD table: `python tools/check_bdd.py --list <feature>`.
   - Test results (suites, counts), firmware size.
   - Manual checklist: `python tools/check_bdd.py --manual <feature>` + screenshots still
     needed + "install the dev build" (CI posts the version as a comment).
   - Docs touched; screenshots inline (`![](docs/screenshots/x.png)` — relative links work in
     PR descriptions only from the branch, so use
     `https://github.com/alasota/CYD-Flight-Tracker/blob/<branch>/docs/screenshots/x.png?raw=1`).
   - Deviations from the spec and anything from `review.md` left non-blocking.
   - End with the Claude Code attribution line.
   Save it as `.claude/work/<slug>/pr.md`.
4. `gh pr create --base main --head <branch> --title "<spec title, English>" --body-file
   .claude/work/<slug>/pr.md`. Without gh: give the owner
   `https://github.com/alasota/CYD-Flight-Tracker/compare/main...<branch>?expand=1` and the
   path of `pr.md` to paste.
5. CI (`.github/workflows/pr.yml`) runs tests, native-asan, the BDD check and the build, then
   publishes `vX.Y.Z-dev.N · PR #M` as a pre-release and comments on the PR. If you have gh,
   wait for it (`gh pr checks --watch`) and report the dev version; on failure read the log
   (`gh run view --log-failed`), fix through the implementer, push again.

## 8. Report

To the owner: PR link, dev version (or "CI running"), what to test on the board (manual
checklist), open points. Don't merge — that's the owner's call. Merging closes the PR, and CI
deletes its dev releases; the version comes with `/release` (a feature = minor bump).
