---
name: release
description: Release what has been merged into main since the last version — pick the version (minor for features, patch for fixes), get the owner's OK, move finished specs to spec/done and fill in vNEXT through a release/ PR, update the project page if needed, then tag main and watch the GitHub Release build. Use when the user says "/release", "wydanie", "wydaj nową wersję", "zrób release".
argument-hint: "[patch | minor | major | vX.Y.Z]"
---

# /release — main → vX.Y.Z

You are the orchestrator of a release. Everything that changes `main` goes through a PR (the
`main` ruleset requires it); only the tag is pushed directly. Shared rules:
`.claude/harness/conventions.md`. `gh` is required here (`gh auth status`); without it, stop and
say how to install it.

## 1. Up to date

```bash
git switch main && git pull --ff-only && git fetch --tags origin
```

`git status --short` may show the owner's unrelated files — fine, but nothing of a release may be
uncommitted. If `main` can't fast-forward (local commits on main), stop and ask.

## 2. The plan

`python tools/release_plan.py` (or `--bump <x>` if the owner passed patch / minor / major; a full
`vX.Y.Z` argument = use exactly that, if it's higher than `latest`). It prints JSON:

- `latest`, `next`, `bump` — highest stable tag and the proposal: **minor** (X.Y+1.0) if any
  merged PR is a feature (`spec/…`, `issue/…` without `bug`), **patch** (X.Y.Z+1) if there are
  only fixes (`fix/…`, label `bug`) and other changes. Major only when the owner says so.
- `prs` — merged into main since `latest`, each with `kind`; `commits_since` — every commit on
  main since `latest` (commits not from a PR are fine, list them).
- `specs_to_move` — `spec/planned/…` that are done; `vnext_files` — files still saying `vNEXT`.
- `stale_docs` — docs that still call a finished spec unfinished, **whenever** it was released
  (ROADMAP row outside "Done" for a spec in `spec/done/`; a `done` row in `spec/README.md`
  without a version), each with `version` = the release that really shipped it (found from
  tags), else the one being made. The PR window can't see these: spec 06 sat "In progress" on
  the project page through v1.6.0-v1.6.3 (implemented before the `vNEXT` rule, v1.6.0 tagged
  by hand).

`bump` is null and `commits_since` is empty → nothing to release; say so and stop — unless
`stale_docs` isn't empty: then offer a docs-only `release/fix-docs` PR (§4 without the tag).

Also check:
- `main`'s CI is green on its HEAD: `gh run list --workflow firmware.yml --branch main --limit 3
  --json headSha,conclusion,status` — the run for `git rev-parse HEAD` must be `success`. Red or
  still running → stop (running: offer to wait).
- Open PRs (`gh pr list --state open`) aren't in the release — mention them.

## 3. The owner decides

Show: `latest` → `next` and why (which PRs are features / fixes), the PR list (number, title,
kind), commits without a PR, what the release PR will change (specs to `done/`, `vNEXT` →
version, ROADMAP, project page), open PRs left out. AskUserQuestion: release `next` / another
bump / cancel. A tag is public and triggers the GitHub Release — no tag without this yes.

## 4. Release PR (only if there is something to update)

Needed when `specs_to_move`, `vnext_files` or `stale_docs` is non-empty, or a merged feature has no
`## Done` row in ROADMAP.md, or the project page lacks something a released feature changed.
Otherwise skip to §5.

1. `git switch -c release/<next> origin/main`.
2. Spawn `docs-writer` in **release mode** with: the version, the PR list (titles, bodies via
   `gh pr view N --json title,body`), `specs_to_move`, `vnext_files`, `stale_docs`. It moves the specs (+ their
   `.feature`) to `spec/done/`, fixes links to them, replaces `vNEXT`, sets ROADMAP / spec status /
   `spec/README.md` to the version, and updates `site/` / README only where a released feature
   isn't described yet. It commits: `Release <next>: specs to done, version in roadmap and docs`.
3. Check the diff yourself (`git diff origin/main...HEAD`): no placeholder left
   (`python tools/release_plan.py` → `"vnext_files": []` and `"stale_docs": []`), `python tools/check_bdd.py` clean,
   `tools/build_site.py`'s `parse_roadmap` still reads ROADMAP.md, nothing but docs changed.
4. `git push -u origin release/<next>`;
   `gh pr create --base main --title "Release <next>" --body-file <body>` — body: the version,
   "Included" (the PR list with links), "This PR" (what it updates); attribution line.
5. `gh pr checks --watch` (the release PR gets its own dev build — ignore it, it's deleted on
   merge). Green → `gh pr merge --rebase --delete-branch` (the owner's yes in §3 covers merging
   this housekeeping PR; if the ruleset still blocks, show why and stop).
6. `git switch main && git pull --ff-only`.

## 5. Tag

```bash
git switch main && git pull --ff-only
git tag -a <next> -m "<next>" -m "<one line per included PR: #N title>"
git push origin <next>
```

Annotated (it shows who and when in the GitHub history); always on `main`'s HEAD after the pull,
never on a PR branch. Then `git log --oneline -1 --decorate` to show it.

## 6. Watch the release

`gh run list --workflow release.yml --limit 1` → `gh run watch <id>`. Success → check the release:
`gh release view <next> --json name,isPrerelease,assets,url` (not a pre-release; assets:
`cyd-sky-tracker-<next>.bin`, `-full.bin`, bootloader, partitions, boot_app0, SHA256SUMS.txt,
version.txt). The Release's notes are GitHub's generated list of the merged PRs. The project page
(`site.yml`, called by release.yml) rebuilds with the new version badge, changelog and roadmap.
Failure → `gh run view <id> --log-failed`, report; don't delete or move the tag without the
owner (a re-run of the failed job is fine: `gh run rerun <id> --failed`).

## 7. Report

Version, release URL, what's in it (PRs), the project page updated, boards will be offered the
update on their next daily check (or *Check now* on the Firmware tab).
