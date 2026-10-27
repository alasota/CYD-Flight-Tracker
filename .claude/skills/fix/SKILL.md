---
name: fix
description: Fix one bug on its own fix/ branch — find the root cause, reproduce it with a failing regression test first (stop for the owner's OK), fix it, review, then a pull request labelled bug whose CI builds a -dev release to check on the board. Use when the user says "/fix <description>", "/fix #14", "napraw błąd …", or pastes a log with "to nie działa".
argument-hint: "<bug description | #issue | issue URL>"
---

# /fix — bug → fix/ branch → PR

You are the **orchestrator**, as in `/implement` (read `.claude/harness/conventions.md` now):
you spawn agents, check their work, talk to the owner, open the PR. A fix is small and targeted —
keep it that way; anything that grows into a feature goes back to the owner as a spec.

One short status line per phase.

## 0. Preconditions

Same as `/implement` §0: unrelated local changes are fine, `git fetch origin`, `gh auth status`.

## 1. Input and branch

- `#14` / an issue URL → `gh issue view 14 --json number,title,body,labels,url,comments`.
  slug = `fix-14-<kebab-title>`, branch = `fix/14-<kebab-title>`.
- A description (and maybe a pasted serial log / screenshot path) → slug =
  `fix-<kebab-summary>` (≤ 5 words), branch = `fix/<kebab-summary>`. If the report is too vague
  to start (no symptom, no place), ask the owner one round of questions first: what was on the
  screen / page, expected vs. actual, the log lines, how to trigger it.
- Branch exists → resume from `.claude/work/<slug>/`. Else
  `git switch -c <branch> origin/main`, `mkdir -p .claude/work/<slug>`, save the report as
  `.claude/work/<slug>/report.md` (verbatim, including log lines).

## 2. Diagnosis + reproducing test — agent `bug-investigator`

Check its result yourself: `diagnosis.md` exists; the new test fails **now**, on the assertion
about the expected behaviour (run `pio test -e native -f test_<suite>` and look); the rest of the
suite passes; the red commit is there.

**Stop and show the owner**: symptom, root cause (`file:line`, mechanism), the reproducing test
and its failure, the proposed fix, anything else with the same pattern. AskUserQuestion: fix it
like this / correct the diagnosis / it's not a bug. If the bug can't be reproduced natively,
say so plainly and what the board check will be.

## 3. Fix — agent `implementer`

Brief it with `diagnosis.md` as the plan: the smallest change that removes the root cause, the
same pattern elsewhere if the diagnosis found it, no refactoring on the side. The regression test
must turn green without being changed. Then verify yourself: `pio test -e native`, `pio run -e cyd`,
`python tools/check_bdd.py`, `grep -rn "TODO(fix" src test` empty.

## 4. Docs — agent `docs-writer` (fix mode)

Only what the fix changes: a ⚠️ pitfall in CLAUDE.md (one line) or the area's `docs/` file where
the next session would trip over the same thing, the story in `docs/history.md`, a corrected
statement in README / CLAUDE.md / `docs/`, the
spec's "Wynik" if the bug was in a spec'd feature (a line "Poprawione w vNEXT: …"), screenshots
if something visible changed. No ROADMAP entry. Skip the agent when there's nothing to document —
say so in the PR.

## 5. Review — agent `reviewer`

As in `/implement` §6, plus: the regression test reproduces the reported symptom (not a nearby
one), it was committed red before the fix, the fix addresses the root cause rather than the
symptom. At most two fix rounds.

## 6. Pull request

1. Final checks; `git push -u origin <branch>`.
2. Body (`.claude/work/<slug>/pr.md`), not the feature template:
   - **Bug** — what the owner saw (quote the report / log lines), expected vs. actual.
   - **Cause** — `file:line`, the mechanism, since when.
   - **Fix** — what changed and why that's enough.
   - **Proof** — the regression test(s): name, failing before (paste the assertion message from
     the red commit), passing now; the other suites.
   - **On the board** — how to check it with the dev build CI posts below.
   - `Closes #N` for an issue. End with the Claude Code attribution line.
3. `gh pr create --base main --head <branch> --title "Fix: <what was wrong, in user terms>"
   --label bug --body-file .claude/work/<slug>/pr.md` (create the label once with
   `gh label create bug` if it's missing). The `fix/` branch and the `bug` label make
   `/release` count it as a patch.
4. `gh pr checks --watch`; report the dev version from the CI comment. CI failure → read
   `gh run view --log-failed`, back to the implementer.

## 7. Report

PR link, dev version, the board check, open points. Don't merge — the owner does. After the
merge, `/release` makes the patch release.
