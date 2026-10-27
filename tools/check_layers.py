#!/usr/bin/env python3
"""Layer / dependency check for the src/<context>/<layer>/ tree (spec 12 §5).

  python tools/check_layers.py [--strict] [--root DIR]

TODO(spec 12): stub from test-author — the API tools/test_check_layers.py expects, with neutral
behaviour (nothing classified, nothing found). The implementer fills it in.
"""

import argparse
import sys
from collections import namedtuple
from pathlib import Path

Finding = namedtuple("Finding", "path line message")

# One #include directive: 1-based line, the name between the quotes / brackets, angle = <…>,
# arduino = the line sits inside an #ifdef ARDUINO (or the #else of #ifndef ARDUINO) branch.
Include = namedtuple("Include", "line name angle arduino")


def format_finding(f):
    return f"{f.path}:{f.line}: {f.message}"


def classify(path):
    """Layer of a repo-relative path ('shared', 'platform', 'app', '<ctx>/<layer>', 'test') or
    None when it sits outside any context and layer. TODO(spec 12): stub from test-author."""
    return None


def find_includes(text):
    """[Include] for every #include directive in text. TODO(spec 12): stub from test-author."""
    return []


def resolve_include(path, name, known):
    """Repo-relative path the quoted include `name` in file `path` resolves to (from src/,
    include/, test/support/ or — for test files — the test's own directory), or None.
    TODO(spec 12): stub from test-author."""
    return None


def check_files(files):
    """[Finding] for {relpath: text}: layer violations, SDK includes, unresolved includes,
    files outside a context. TODO(spec 12): stub from test-author."""
    return []


def full_path_findings(files):
    """[Finding] for quoted includes in src/ that resolve only from the including file's own
    directory. TODO(spec 12): stub from test-author."""
    return []


def load_tree(root, dirs=("src", "test", "include")):
    """{relpath: text} of the source files under root/dirs. TODO(spec 12): stub."""
    return {}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--strict", action="store_true", help="exit 1 when anything is found")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.parse_args(argv)
    return 0  # TODO(spec 12): stub from test-author


if __name__ == "__main__":
    sys.exit(main())
