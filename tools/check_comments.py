#!/usr/bin/env python3
"""Comment / docs hygiene check (spec 11).

  python tools/check_comments.py [--strict] [--root DIR]

Finds, in src/ and test/:
  * symbols removed in spec 11 (dead code that must not come back),
  * comments in network-core modules (net_task, core 0) that credit main.cpp / loopTask,
  * doc references (`CLAUDE.md "X"`, `docs/<f>.md "X"`) that don't hit a heading,
  * the old `CLAUDE.md review notes X.Y` form,
and checks that CLAUDE.md stays under 250 lines. Warns only (exit 0) unless --strict.

TODO(spec 11): stub from test-author — the functions below return nothing yet.
"""

import argparse
import sys
from collections import namedtuple
from pathlib import Path

Finding = namedtuple("Finding", "path line message")


def format_finding(f):
    return f"{f.path}:{f.line}: {f.message}"


# TODO(spec 11): stub from test-author — the removed symbols of spec 11 §1.
REMOVED_SYMBOLS = []

# TODO(spec 11): stub from test-author — modules (file stems in src/) that run in net_task.
NET_MODULES = set()


def extract_comments(text):
    """[(line, comment text)] for every // and /* */ comment line."""
    return []  # TODO(spec 11): stub from test-author


def find_removed_symbols(files):
    """files: {relpath: text}. Findings for every removed symbol (code or comment)."""
    return []  # TODO(spec 11): stub from test-author


def find_main_cpp_comments(files):
    """Findings for comments mentioning main.cpp / loopTask in net modules."""
    return []  # TODO(spec 11): stub from test-author


def find_bad_doc_refs(files, docs):
    """docs: {relpath: markdown}. Findings for doc references that don't resolve."""
    return []  # TODO(spec 11): stub from test-author


def check_claude_md_size(text, limit=250):
    """[Finding] when CLAUDE.md has `limit` lines or more."""
    return []  # TODO(spec 11): stub from test-author


def load_tree(root, dirs=("src", "test")):
    """{posix relpath: text} of the source files under root/dirs."""
    return {}  # TODO(spec 11): stub from test-author


def load_docs(root):
    """{posix relpath: text} of CLAUDE.md and docs/*.md under root."""
    return {}  # TODO(spec 11): stub from test-author


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.parse_args(argv)
    return 0  # TODO(spec 11): stub from test-author


if __name__ == "__main__":
    sys.exit(main())
