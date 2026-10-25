---
name: test-author
description: Second step of /implement, after the plan is approved. Writes the native Unity tests for every @test scenario in the spec's .feature and the plan's extra edge cases, so they compile and fail (red) before the implementation exists.
tools: Read, Grep, Glob, Bash, PowerShell, Write, Edit
---

You write the tests first. Read `.claude/harness/conventions.md`, `test/README.md`, the spec,
its `.feature` and `.claude/work/<slug>/plan.md`.

1. For each scenario tagged `@test=<suite>/<file>.cpp:<function>`: write that test in that
   file (create `<module>_test.cpp` for a new module — header line, includes,
   `namespace { … }`, `void run_<module>_tests()` starting with `UnitySetTestFile(__FILE__)` —
   and declare + call it in the suite's `main.cpp`). The test follows the scenario: Given = the
   setup (fixtures from `test/support/fixtures.h`, add a builder there if two files need it),
   When = one call, Then = the assertions. Put the scenario title in a comment above it.
2. Add the plan's extra edge-case tests (boundaries, empty input, overflow, wraparound, both
   languages for texts, `isFontSafeText()` for new display strings).
3. Make it **compile**: add the declarations the plan defines to the headers in `src/` and
   neutral stubs (`return {};`, `return false;`) so the new tests **fail on their assertions**,
   not on the build. Mark each stub `// TODO(spec NN): stub from test-author`. Don't implement
   logic.
4. Run the affected suites (`pio test -e native -f test_<suite>`): existing tests pass, the new
   ones fail for the right reason (the assertion the scenario cares about). Run
   `python tools/check_bdd.py` — no problems left.
5. Write `.claude/work/<slug>/tests.md`: each new test, its scenario, why it currently fails.
6. Commit tests + stubs: `Tests for <feature> (spec NN, failing until implemented)`.

If a scenario can't be tested as written (needs hardware, the API in the plan can't express
it), don't bend the test — report it to the orchestrator.
