# Spec → PR harness: shared rules for every agent

Read this before you start. The project rules are in `CLAUDE.md` (read the sections the plan
names), the test rules in `test/README.md`. This file only adds what the harness needs.

## The run

Three orchestrators, each a skill run by the main session; they spawn the agents below one at a
time (agents don't spawn agents):

| Command | Branch | Ends with |
|---|---|---|
| `/implement <spec \| #issue>` (`.claude/skills/implement/`) | `spec/NN-name`, `issue/N-name` | a PR (feature → minor release) |
| `/fix <description \| #issue>` (`.claude/skills/fix/`) | `fix/N-name`, `fix/name` | a PR labelled `bug` (→ patch release) |
| `/release [patch\|minor\|major]` (`.claude/skills/release/`) | `release/vX.Y.Z` (only if docs need it) | an annotated tag on main + the GitHub Release |

| Agent | Writes | Never touches |
|---|---|---|
| `spec-analyst` | `.claude/work/<slug>/plan.md`, `spec/planned/<NN-name>.feature`, a new spec file for an issue | `src/`, `test/` |
| `bug-investigator` (/fix) | `.claude/work/<slug>/diagnosis.md`, the failing regression test (+ a stub if needed) | the fix itself |
| `test-author` | `test/**`, declarations + neutral stubs in `src/*.h` / `src/*.cpp` so the tests compile and fail | real logic in `src/` |
| `implementer` | `src/**`, `platformio.ini`, tools; tests only to fix a test that is provably wrong (say so) | the `.feature`, the spec's requirements |
| `docs-writer` (feature / fix / release mode) | `CLAUDE.md`, `README.md`, `test/README.md`, `ROADMAP.md`, `spec/**` status, `site/`, `docs/screenshots/` | `src/`, `test/` |
| `reviewer` | `.claude/work/<slug>/review.md` | everything else (read-only) |

Hand-over between agents is through `.claude/work/<slug>/` (git-ignored): `plan.md` (analyst),
`tests.md` (test-author: which tests, which are red and why), `impl.md` (implementer: what was
done per step, deviations), `docs.md` (docs-writer), `review.md` (reviewer). Append, don't
rewrite another agent's file. Your final message to the orchestrator: what you did, what is
left, anything that needs the owner's decision — short.

## Environment (Windows, this machine)

- PlatformIO is not on the Bash PATH: start Bash commands with
  `export PATH="$PATH:$USERPROFILE/.platformio/penv/Scripts"` (or use the PowerShell tool, where
  `pio` works).
- `pio test -e native` (~30 s), one suite `-f test_<suite>`, one module
  `PLATFORMIO_BUILD_FLAGS="-DTEST_MODULE=<module>" pio test -e native -f test_<suite>`.
- `ERRORED` with `[WinError 4551]` and no failed assertion = Windows application control
  blocked the fresh test `.exe`. Rerun that suite (up to 3×); it is not a test failure.
- `native-asan` doesn't run on Windows (no sanitizer runtime) — CI runs it.
- Firmware build: `pio run -e cyd`. It must stay under the OTA slot (1.875 MB).
- `python tools/check_bdd.py` — every scenario backed by a test; `--list` / `--manual FILE`.
- Files may have CRLF or LF line endings; keep what the file has.
- The board is usually at `cyd-sky.local` (password in `$CYD_WEB_PASSWORD`, may be empty).
  **Never flash or reconfigure the board without the orchestrator having asked the owner.**
  Reading `/perf.json`, `/light.json`, `/screenshot.bmp` is fine.

## Testing rules (non-negotiable, from CLAUDE.md "Testing")

- Test first: tests for the new logic exist and fail before the logic is written.
- Pure logic in its own module, hardware/SDK only in thin `#ifdef ARDUINO` adapters; everything
  pure gets a native Unity test in the suite of its context (`test/README.md` table).
- BDD: each Scenario in the spec's `.feature` is verified by a test
  (`@test=<suite>/<file>.cpp:<function>`) or by hand on the board (`@manual`). Gherkin keywords
  in English (`Feature/Scenario/Given/When/Then/And/But`), scenario text in the spec's language.
  A scenario's test is named after it (`test_<scenario_in_snake_case>`, shortened if needed) and
  checks the scenario's Then — not something easier.
- Assertions that print values (`ASSERT_ENUM_EQ`, `ASSERT_STR_EQ`, `ASSERT_CONTAINS`,
  `assertJson*`), fixtures from `test/support/fixtures.h`; new global state gets its reset in
  `resetAllForTesting()`. `-Wall -Wextra -Werror`.
- Edge cases the spec names (boundaries, empty, null, overflow, wraparound) each get a test.

## Versions: vNEXT

A branch doesn't know its version. Docs that need it (ROADMAP "Done in …", spec status
"wydane w …", `spec/README.md`'s "Stan") say `vNEXT`; `/release` replaces it with the number
(`tools/release_plan.py` lists the files). The project page renders it as "the next release".
Features → minor (X.Y+1.0), fixes → patch (X.Y.Z+1), decided from the merged branches
(`spec/`, `issue/` = feature; `fix/` or label `bug` = fix). Tags only on main, after the merge.

## Commits

On the work branch only (`spec/…`, `issue/…`, `fix/…`, `release/…`), never on `main` — main
takes changes through PRs only (its ruleset), only the release tag is pushed directly. One logical
step per commit, tests green after each (except the test-author's "red" commit, which says so).
Subjects in English, written to read well in the project page's changelog (they are shown there
as they are). End every message with the `Co-Authored-By:` line Claude Code gives you. Stage
files by name — the owner may have unrelated changes in the working tree; never `git add -A`,
never commit `.vscode/` or `__pycache__/`.

## Screenshots

- Settings page changed → `python tools/screenshots.py` (no board needed; renders from
  `config_page_test.cpp` in `test_admin`). New page / tab → extend `tools/screenshots.py` and
  the render test with it.
- Display changed → a PNG of each changed screen from the board (`/screenshot.bmp?screen=N`,
  `tools/screenshots.py --device`), which needs the branch's firmware on the board: ask, through
  the orchestrator, whether to flash it (`pio run -e cyd-ota -t upload` or USB). Without a board,
  leave a checklist item in the PR ("screenshot after installing the dev build").
- Screenshots live in `docs/screenshots/`, referenced from README.md (and the site, which
  copies the folder).
