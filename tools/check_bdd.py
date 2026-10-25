#!/usr/bin/env python3
"""BDD scenarios (spec/**/*.feature) must be backed by tests.

Every spec may have a Gherkin file next to it, same name: spec/planned/05-x.md ->
spec/planned/05-x.feature. Each Scenario (or Scenario Outline) carries tags that
say how it is verified:

  @test=test_enrichment/lookup_cache_test.cpp:test_evicts_least_recently_used
      a native Unity test: path relative to test/, then the test function. The
      file must define the function and RUN_TEST() it. Several @test tags are fine.
  @test=tools/test_build_site.py:ChangelogHtmlTest.test_text_is_escaped
      a Python unittest under tools/: path relative to the repo root, then
      <Class>.<method>. The class must define the method and its name must start
      with "test" (unittest runs only those). Both kinds may back one scenario.
  @manual
      checked by hand on the board (listed in the PR's checklist, see --manual).

Checks: every scenario has at least one @test or @manual, every @test points at a
test that exists, every scenario has a Then step, the .feature has a Feature: line
and its .md spec next to it. Exit 1 with one line per problem.

  python tools/check_bdd.py                 # check everything under spec/
  python tools/check_bdd.py --manual FILE   # Markdown checklist of @manual scenarios
  python tools/check_bdd.py --list FILE     # scenario -> test table (Markdown)
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STEP = re.compile(r"^(Given|When|Then|And|But|\*)\b")
SCENARIO = re.compile(r"^(Scenario|Scenario Outline|Scenario Template|Example):\s*(.+)$")
TEST_TAG = re.compile(r"^@test=([\w./-]+\.cpp):(\w+)$")
PY_TEST_TAG = re.compile(r"^@test=(tools/[\w./-]+\.py):(\w+)\.(\w+)$")


class Scenario:
    def __init__(self, title, line, tags):
        self.title = title
        self.line = line
        self.tags = tags
        self.steps = []  # keyword of each step, "And"/"But" resolved to the previous one

    @property
    def tests(self):
        """[(file, name)]: (suite/file.cpp, function) or (tools/file.py, "Class.method")."""
        out = []
        for t in self.tags:
            m = TEST_TAG.match(t)
            if m:
                out.append(m.groups())
                continue
            m = PY_TEST_TAG.match(t)
            if m:
                out.append((m.group(1), f"{m.group(2)}.{m.group(3)}"))
        return out

    @property
    def manual(self):
        return "@manual" in self.tags


def parse_feature(text):
    """(feature title or None, [Scenario]) — a small Gherkin subset, enough for the specs."""
    feature = None
    scenarios = []
    pending_tags = []
    in_docstring = False
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if line.startswith('"""') or line.startswith("```"):
            in_docstring = not in_docstring
            continue
        if in_docstring or not line or line.startswith("#"):
            continue
        if line.startswith("@"):
            pending_tags += line.split()
            continue
        if line.startswith("Feature:"):
            feature = line[len("Feature:"):].strip()
            pending_tags = []
            continue
        m = SCENARIO.match(line)
        if m:
            scenarios.append(Scenario(m.group(2).strip(), n, pending_tags))
            pending_tags = []
            continue
        if line.startswith(("Background:", "Rule:", "Examples:", "Scenarios:")):
            pending_tags = []
            continue
        s = STEP.match(line)
        if s and scenarios:
            kw = s.group(1)
            sc = scenarios[-1]
            if kw in ("And", "But", "*"):
                kw = sc.steps[-1] if sc.steps else kw
            sc.steps.append(kw)
    return feature, scenarios


def test_defined(test_root, rel, func):
    """'' if test/<rel> defines and RUN_TEST()s func, else the reason."""
    path = test_root / rel
    if not path.is_file():
        return f"test file test/{rel} does not exist"
    src = path.read_text(encoding="utf-8", errors="replace")
    if not re.search(r"\b%s\s*\(\s*(void)?\s*\)\s*\{" % re.escape(func), src):
        return f"test/{rel} has no function {func}()"
    if not re.search(r"RUN_TEST\(\s*%s\s*\)" % re.escape(func), src):
        return f"test/{rel} never runs {func} (RUN_TEST)"
    return ""


def py_test_defined(repo_root, rel, name):
    """'' if <repo_root>/<rel> has class Class with a test* method, else the reason."""
    cls, method = name.split(".", 1)
    path = repo_root / rel
    if not path.is_file():
        return f"test file {rel} does not exist"
    src = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^class\s+%s\b.*:[ \t]*$" % re.escape(cls), src, re.M)
    if not m:
        return f"{rel} has no class {cls}"
    # The class body runs until the next top-level line (class, def, if __name__ ...).
    end = re.search(r"^\S", src[m.end():], re.M)
    body = src[m.end():m.end() + end.start()] if end else src[m.end():]
    if not re.search(r"^[ \t]+def\s+%s\s*\(\s*self\b" % re.escape(method), body, re.M):
        return f"{rel} has no method {method} in class {cls}"
    if not method.startswith("test"):
        return f"{rel}: {cls}.{method} isn't named test*, unittest never runs it"
    return ""


def check_file(feature_path, test_root):
    problems = []
    rel = feature_path.relative_to(REPO) if feature_path.is_relative_to(REPO) else feature_path

    def bad(line, msg):
        problems.append(f"{rel}:{line}: {msg}")

    feature, scenarios = parse_feature(feature_path.read_text(encoding="utf-8"))
    if feature is None:
        bad(1, "no 'Feature:' line")
    if not feature_path.with_suffix(".md").is_file():
        bad(1, f"no spec next to it ({feature_path.with_suffix('.md').name})")
    if not scenarios:
        bad(1, "no scenarios")
    titles = set()
    for sc in scenarios:
        if sc.title in titles:
            bad(sc.line, f"duplicate scenario title '{sc.title}'")
        titles.add(sc.title)
        if not sc.tests and not sc.manual:
            bad(sc.line, f"scenario '{sc.title}' has neither @test=<file>:<function> nor @manual")
        for t in sc.tags:
            if t.startswith("@test") and not (TEST_TAG.match(t) or PY_TEST_TAG.match(t)):
                bad(sc.line, f"malformed tag {t} (expected @test=<suite>/<file>.cpp:<function> "
                             f"or @test=tools/<file>.py:<Class>.<method>)")
        if "Then" not in sc.steps:
            bad(sc.line, f"scenario '{sc.title}' has no Then step")
        for f, func in sc.tests:
            if f.endswith(".py"):  # Python paths are relative to the repo root, not test/
                why = py_test_defined(test_root.parent, f, func)
            else:
                why = test_defined(test_root, f, func)
            if why:
                bad(sc.line, f"scenario '{sc.title}': {why}")
    return problems


def markdown_list(feature_path):
    _, scenarios = parse_feature(feature_path.read_text(encoding="utf-8"))
    rows = ["| Scenario | Verified by |", "|---|---|"]
    for sc in scenarios:
        how = [f"`{f}` `{fn}`" for f, fn in sc.tests] + (["on the board (manual)"] if sc.manual else [])
        rows.append(f"| {sc.title} | {'<br>'.join(how) or '**nothing**'} |")
    return "\n".join(rows)


def markdown_manual(feature_path):
    _, scenarios = parse_feature(feature_path.read_text(encoding="utf-8"))
    items = [f"- [ ] {sc.title}" for sc in scenarios if sc.manual]
    return "\n".join(items) or "_No manual scenarios._"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manual", metavar="FEATURE", help="print a checklist of the @manual scenarios")
    ap.add_argument("--list", metavar="FEATURE", help="print a scenario -> test table")
    ap.add_argument("--root", default=str(REPO), help=argparse.SUPPRESS)
    args = ap.parse_args(argv)
    if args.manual:
        print(markdown_manual(Path(args.manual)))
        return 0
    if args.list:
        print(markdown_list(Path(args.list)))
        return 0
    root = Path(args.root)
    features = sorted((root / "spec").rglob("*.feature"))
    problems = []
    for f in features:
        problems += check_file(f, root / "test")
    for p in problems:
        print(p)
    n = sum(len(parse_feature(f.read_text(encoding="utf-8"))[1]) for f in features)
    print(f"check_bdd: {len(features)} feature file(s), {n} scenario(s), {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
