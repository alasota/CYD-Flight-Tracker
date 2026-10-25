## What and why

<!-- One paragraph: what changes for the person looking at the display / settings page. -->

Spec: <!-- spec/planned/NN-name.md (moved to spec/done/ in this PR) --> · Issue: <!-- Closes #N, or "—" -->

## How it is verified

### BDD scenarios

<!-- python tools/check_bdd.py --list spec/.../NN-name.feature -->

### Native tests

- [ ] `pio test -e native` — all suites PASSED (new / changed tests: …)
- [ ] `python tools/check_bdd.py` — no problems
- [ ] `pio run -e cyd` builds
- CI also runs `native-asan` and publishes a **development build** (comment below)

### On the board (manual scenarios)

<!-- python tools/check_bdd.py --manual spec/.../NN-name.feature
     Install the dev build: settings → Firmware → "Show development versions" → Install. -->

## Docs and screenshots

- [ ] CLAUDE.md / README.md / test/README.md / ROADMAP.md / spec status updated where it applies
- [ ] Screenshots regenerated (`python tools/screenshots.py [--device cyd-sky.local]`) — or "no visible change"

<!-- Changed screenshots inline: ![](docs/screenshots/....png) -->
