"""Tests for tools/check_bdd.py:  python tools/test_check_bdd.py"""

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_bdd  # noqa: E402

TEST_CPP = """\
namespace {
static void test_evicts_oldest(void) { TEST_ASSERT_TRUE(true); }
static void test_defined_not_run(void) {}
}  // namespace
void run_x_tests() {
  RUN_TEST(test_evicts_oldest);
}
"""

TEST_PY = """\
import unittest


class XTest(unittest.TestCase):
    def test_a(self):
        self.assertTrue(True)

    def helper(self):
        pass


class OtherTest(unittest.TestCase):
    def test_b(self):
        pass


if __name__ == "__main__":
    unittest.main()
"""


class CheckBddTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "spec" / "planned").mkdir(parents=True)
        (self.root / "test" / "test_enrichment").mkdir(parents=True)
        (self.root / "test" / "test_enrichment" / "cache_test.cpp").write_text(TEST_CPP)
        (self.root / "spec" / "planned" / "05-x.md").write_text("# Spec 05\n")

    def tearDown(self):
        self.tmp.cleanup()

    def feature(self, body, name="05-x.feature"):
        p = self.root / "spec" / "planned" / name
        p.write_text(textwrap.dedent(body))
        return p

    def problems(self, path):
        return check_bdd.check_file(path, self.root / "test")

    def test_backed_scenarios_pass(self):
        p = self.feature("""\
            Feature: Bounded cache
              @test=test_enrichment/cache_test.cpp:test_evicts_oldest
              Scenario: Oldest entry goes first
                Given a full cache
                When a new aircraft is looked up
                Then the least recently used entry is evicted
                And the new one is cached

              @manual
              Scenario: Heap stays flat for a day on the board
                Given the board runs for 24 h
                Then the free heap does not shrink
            """)
        self.assertEqual([], self.problems(p))

    def test_scenario_without_tag_fails(self):
        p = self.feature("""\
            Feature: F
              Scenario: Untested
                Given x
                Then y
            """)
        [msg] = self.problems(p)
        self.assertIn("neither @test", msg)
        self.assertIn(":2:", msg)  # line of the Scenario

    def test_missing_file_function_or_run_test(self):
        p = self.feature("""\
            Feature: F
              @test=test_enrichment/nope_test.cpp:test_a
              Scenario: A
                Then a
              @test=test_enrichment/cache_test.cpp:test_missing
              Scenario: B
                Then b
              @test=test_enrichment/cache_test.cpp:test_defined_not_run
              Scenario: C
                Then c
            """)
        msgs = self.problems(p)
        self.assertEqual(3, len(msgs))
        self.assertIn("does not exist", msgs[0])
        self.assertIn("has no function test_missing", msgs[1])
        self.assertIn("never runs test_defined_not_run", msgs[2])

    def test_needs_then_and_feature_and_spec(self):
        p = self.feature("""\
            @manual
            Scenario: No outcome
              Given x
              When y
            """, name="06-orphan.feature")
        msgs = " | ".join(self.problems(p))
        self.assertIn("no 'Feature:' line", msgs)
        self.assertIn("no spec next to it", msgs)
        self.assertIn("has no Then step", msgs)

    def test_and_after_then_counts_and_malformed_tag(self):
        p = self.feature("""\
            Feature: F
              @test=cache_test.cpp
              Scenario: Bad tag
                Given x
                Then y
                And z
            """)
        msgs = self.problems(p)
        self.assertTrue(any("malformed tag" in m for m in msgs))
        self.assertFalse(any("no Then" in m for m in msgs))

    def test_outline_docstring_and_duplicates(self):
        p = self.feature('''\
            Feature: F
              @manual
              Scenario Outline: Radius <r>
                Given the radius is <r>
                Then the cost is shown
                """
                Scenario: not a scenario, inside a docstring
                """
                Examples:
                  | r |
                  | 1 |
              @manual
              Scenario Outline: Radius <r>
                Then again
            ''')
        msgs = self.problems(p)
        self.assertEqual(1, len(msgs))
        self.assertIn("duplicate scenario title", msgs[0])

    def test_markdown_outputs(self):
        p = self.feature("""\
            Feature: F
              @test=test_enrichment/cache_test.cpp:test_evicts_oldest
              Scenario: Auto
                Then a
              @manual
              Scenario: By hand
                Then b
            """)
        self.assertEqual("- [ ] By hand", check_bdd.markdown_manual(p))
        table = check_bdd.markdown_list(p)
        self.assertIn("| Auto | `test_enrichment/cache_test.cpp` `test_evicts_oldest` |", table)
        self.assertIn("| By hand | on the board (manual) |", table)

    # ---- Python tests (spec 13): @test=tools/<file>.py:<Class>.<method>

    def python_test_file(self):
        (self.root / "tools").mkdir(exist_ok=True)
        (self.root / "tools" / "test_x.py").write_text(TEST_PY)

    def test_python_test_tag_backed(self):
        self.python_test_file()
        p = self.feature("""\
            Feature: F
              @test=tools/test_x.py:XTest.test_a
              Scenario: Python-backed
                Then a
              @test=tools/test_x.py:OtherTest.test_b
              @test=test_enrichment/cache_test.cpp:test_evicts_oldest
              Scenario: Both kinds
                Then b
            """)
        self.assertEqual([], self.problems(p))
        table = check_bdd.markdown_list(p)
        self.assertIn("`tools/test_x.py`", table)
        self.assertIn("XTest.test_a", table)
        self.assertNotIn("**nothing**", table)

    def test_python_test_tag_missing_class_or_method(self):
        self.python_test_file()
        p = self.feature("""\
            Feature: F
              @test=tools/test_nope.py:XTest.test_a
              Scenario: A
                Then a
              @test=tools/test_x.py:NopeTest.test_a
              Scenario: B
                Then b
              @test=tools/test_x.py:XTest.test_b
              Scenario: C
                Then c
              @test=tools/test_x.py:XTest.helper
              Scenario: D
                Then d
            """)
        msgs = self.problems(p)
        self.assertEqual(4, len(msgs), msgs)
        self.assertIn("does not exist", msgs[0])
        self.assertIn("has no class NopeTest", msgs[1])
        self.assertIn("has no method test_b", msgs[2])  # test_b is OtherTest's, not XTest's
        self.assertIn("helper", msgs[3])  # unittest never runs a method not named test*
        self.assertFalse(any("malformed tag" in m for m in msgs), msgs)

    def test_python_tag_outside_tools_or_without_method_is_malformed(self):
        self.python_test_file()
        p = self.feature("""\
            Feature: F
              @test=tools/test_x.py:XTest
              Scenario: No method
                Then a
              @test=test_enrichment/test_x.py:XTest.test_a
              Scenario: Not under tools
                Then b
            """)
        msgs = self.problems(p)
        self.assertEqual(2, sum("malformed tag" in m for m in msgs), msgs)

    def test_main_over_a_tree(self):
        self.feature("""\
            Feature: F
              Scenario: Untested
                Then y
            """)
        self.assertEqual(1, check_bdd.main(["--root", str(self.root)]))
        (self.root / "spec" / "planned" / "05-x.feature").unlink()
        self.assertEqual(0, check_bdd.main(["--root", str(self.root)]))


if __name__ == "__main__":
    unittest.main()
