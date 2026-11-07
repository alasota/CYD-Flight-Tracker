#!/usr/bin/env python3
"""Rename C/C++ identifiers in src/ and test/ after the spec 12 glossary (stage 7).

  python tools/rename_symbols.py KEY [--check] [--root DIR]
  python tools/rename_symbols.py --list

TODO(spec 12): stub from test-author -- the table, the scanner and the CLI are not written yet.
tools/test_rename_symbols.py describes what they do.
"""

import sys

# [(key, [(old, new), ...], kind)], kind "compound" / "word" / "prefix".
# TODO(spec 12): stub from test-author
RENAMES_STAGE_7 = []


def rename_text(text, pairs, kind):
    """(new text, [(line, old name)] of the comment hits left for review)."""
    # TODO(spec 12): stub from test-author
    return text, []


def main(argv=None):
    # TODO(spec 12): stub from test-author
    return 0


if __name__ == "__main__":
    sys.exit(main())
