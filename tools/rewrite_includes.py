#!/usr/bin/env python3
"""Move the flat src/ files into src/<context>/<layer>/ and give every #include a full path
from src/ (spec 12, stage 1).

  python tools/rewrite_includes.py [--move] [--check] [--root DIR]

TODO(spec 12): stub from test-author — the API tools/test_rewrite_includes.py expects, with
neutral behaviour (no moves, nothing rewritten). The implementer fills it in.
"""

import argparse
import sys
from pathlib import Path

# {path relative to src/ today: path relative to src/ after stage 1}
MOVES = {}  # TODO(spec 12): stub from test-author


def build_index(src_paths):
    """{basename: src-relative path} of the headers in src_paths; ValueError naming both paths
    on a duplicate basename. TODO(spec 12): stub from test-author."""
    return {}


def rewrite_text(text, index):
    """text with every #include "X" that doesn't resolve from src/ rewritten to index's full
    path. TODO(spec 12): stub from test-author."""
    return text


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--move", action="store_true", help="git mv the files in MOVES")
    ap.add_argument("--check", action="store_true", help="exit 1 if anything would change")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.parse_args(argv)
    return 0  # TODO(spec 12): stub from test-author


if __name__ == "__main__":
    sys.exit(main())
