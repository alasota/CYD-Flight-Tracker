#!/usr/bin/env python3
"""Rename C/C++ identifiers in src/ and test/ after the spec 12 glossary (stage 7).

  python tools/rename_symbols.py KEY [--check] [--report] [--root DIR]
  python tools/rename_symbols.py --list

  KEY       an entry of RENAMES_STAGE_7 (--list prints them).
  (default) rename the entry's identifiers in src/ and test/ (.h .hpp .c .cpp, without the
            generated src/admin/core/web_assets.h and src/display/core/theme/fonts/*), then
            print `path:line` of every comment hit left for review by hand.
  --check   write nothing; print `path:line` of every line the rewrite would change, exit 1 if
            there is one.
  --report  also list whole-word hits of the old names in the other tracked text files
            (docs, tools, the .feature, platformio.ini) - they are edited by hand or not at all.

Why a lexer and not `sed 's/\\bX\\b/Y/g'`: a whole-word regex doesn't know about strings, so it
would rewrite NVS keys, backup JSON keys, log text, hexdb.io's "Aircraft not found." body and
the doc headings comments cite (`docs/data-sources.md "Aircraft identification"`). Here code
identifiers are replaced as whole tokens; "strings" (with escapes), 'chars', raw strings
R"d(...)d" and #include paths never change. In comments a "compound" name (`AircraftRow`) is
replaced outside "quoted text"; a "word" name (`Aircraft`, `Config`, also English words) is only
reported, so the commit edits the comments that mean the type by hand. A "prefix" entry renames
`configPortal` + an upper-case letter, which leaves WiFiManager's `setConfigPortalBlocking()`
and the SDK's `configTzTime()` alone (no token starts with `configPortal` there).
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}
SCOPE_DIRS = ("src", "test")
GENERATED = ("src/admin/core/web_assets.h", "src/display/core/theme/fonts/")

# [(key, [(old, new), ...], kind)] in the order of the stage-7 commits (spec 12 section 3;
# `LookupPorts` is a section 4.1 name). One entry per commit; a type and the functions named
# after it go together.
RENAMES_STAGE_7 = [
    ("BoundingBox", [("BoundingBox", "ScanArea"),
                     ("computeBoundingBox", "computeScanArea")], "compound"),
    ("OpenSkyHealth", [("OpenSkyHealth", "FeedHealth")], "compound"),
    ("DataSnapshot", [("DataSnapshot", "AirspaceSnapshot"),
                      ("buildDataSnapshot", "buildAirspaceSnapshot")], "compound"),
    ("LookupPorts", [("LookupPorts", "EnrichmentPort")], "compound"),
    ("AircraftRow", [("AircraftRow", "TrackedFlight")], "compound"),
    ("Aircraft", [("Aircraft", "AircraftState")], "word"),
    ("configPortal", [("configPortal", "webServer")], "prefix"),
    ("Config", [("Config", "Settings")], "word"),
    ("settingsFunctions", [("defaultConfig", "defaultSettings"),
                           ("sanitizeConfig", "sanitizeSettings"),
                           ("loadConfig", "loadSettings"),
                           ("saveConfig", "saveSettings"),
                           ("reloadConfig", "reloadSettings"),
                           ("configToBackupJson", "settingsToBackupJson"),
                           ("restoreConfigFromBackupJson", "restoreSettingsFromBackupJson"),
                           ("makeConfig", "makeSettings")], "compound"),
]

KINDS = ("compound", "word", "prefix")

# Literal prefixes: L"x" u8"x" ... and raw strings R"d(...)d", LR"..." ...
_ENCODING_PREFIXES = {"L", "u", "U", "u8"}
_RAW_PREFIXES = {"R", "LR", "uR", "UR", "u8R"}
_ANGLE_INCLUDE_RE = re.compile(r"#[ \t]*include[ \t]*<[^>\n]*>")


def _ident_char(c):
    return c.isalnum() or c == "_"


def _renamer(pairs, kind):
    """token -> new token, or None if `token` isn't one of the names."""
    if kind not in KINDS:
        raise ValueError(f"unknown kind {kind!r}")
    table = dict(pairs)
    if kind != "prefix":
        return table.get

    def rename(token):
        for old, new in table.items():
            if token.startswith(old) and len(token) > len(old) and token[len(old)].isupper():
                return new + token[len(old):]
        return None
    return rename


def _string_end(text, i, quote):
    """Index after the "string" or 'char' whose opening quote is at i (an unclosed one ends
    at the end of its line)."""
    j, n = i + 1, len(text)
    while j < n and text[j] != quote and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
    return min(j + 1, n)


def _raw_string_end(text, i):
    """Index after the raw string whose `"` is at i: R"delim( ... )delim"."""
    p = text.find("(", i + 1)
    if p < 0:
        return _string_end(text, i, '"')
    end = text.find(")" + text[i + 1:p] + '"', p)
    return len(text) if end < 0 else end + p - i + 1


def _comment(segment, line, rename, kind, review):
    """`segment` (one comment) with its names replaced outside "quoted text" (compound /
    prefix); hits left as they are go to `review` as (line, old name). A quote doesn't run
    over a line end; an apostrophe is not a quote."""
    out, i, n, quoted = [], 0, len(segment), False
    while i < n:
        c = segment[i]
        if c == "\n":
            line += 1
            quoted = False
        elif c == '"':
            quoted = not quoted
        elif _ident_char(c):
            j = i
            while j < n and _ident_char(segment[j]):
                j += 1
            token = segment[i:j]
            new = None if token[0].isdigit() else rename(token)
            if new is not None and (kind == "word" or quoted):
                if (line, token) not in review:
                    review.append((line, token))
                new = None
            out.append(token if new is None else new)
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def rename_text(text, pairs, kind):
    """(text with `pairs` renamed, [(line, old name)] of the comment hits left for review).

    `kind` "compound": code and comments (outside "quoted text"); "word": code only, every
    comment hit reported; "prefix": like compound, for identifiers that start with the old
    name followed by an upper-case letter. Lines are 1-based; line endings are kept."""
    rename = _renamer(pairs, kind)
    out, review = [], []
    i, n, line = 0, len(text), 1

    def emit(segment):
        nonlocal line
        out.append(segment)
        line += segment.count("\n")

    while i < n:
        c = text[i]
        if text.startswith("//", i) or text.startswith("/*", i):
            if c == "/" and text[i + 1] == "/":
                j = text.find("\n", i)
                j = n if j < 0 else j
            else:
                j = text.find("*/", i + 2)
                j = n if j < 0 else j + 2
            new = _comment(text[i:j], line, rename, kind, review)
            out.append(new)
            line += new.count("\n")
            i = j
        elif c == '"' or c == "'":
            j = _string_end(text, i, c)
            emit(text[i:j])
            i = j
        elif c == "#" and _ANGLE_INCLUDE_RE.match(text, i):
            j = _ANGLE_INCLUDE_RE.match(text, i).end()
            emit(text[i:j])
            i = j
        elif c.isdigit():
            # A number (with ' digit separators) is not an identifier: 0x1Config is one token.
            j = i + 1
            while j < n and (_ident_char(text[j]) or text[j] == "."
                             or (text[j] == "'" and j + 1 < n and _ident_char(text[j + 1]))):
                j += 1
            emit(text[i:j])
            i = j
        elif _ident_char(c):
            j = i
            while j < n and _ident_char(text[j]):
                j += 1
            token = text[i:j]
            if j < n and text[j] == '"' and token in _RAW_PREFIXES:
                k = _raw_string_end(text, j)
                emit(text[i:k])
                i = k
            elif j < n and text[j] in "\"'" and token in _ENCODING_PREFIXES:
                k = _string_end(text, j, text[j])
                emit(text[i:k])
                i = k
            else:
                new = rename(token)
                emit(token if new is None else new)
                i = j
        else:
            emit(c)
            i += 1
    return "".join(out), review


def _entry(key):
    for entry_key, pairs, kind in RENAMES_STAGE_7:
        if entry_key == key:
            return pairs, kind
    return None


def _in_scope(rel):
    return Path(rel).suffix in SOURCE_SUFFIXES and not rel.startswith(GENERATED)


def _sources(root):
    for d in SCOPE_DIRS:
        base = root / d
        if base.is_dir():
            for p in sorted(base.rglob("*")):
                rel = p.relative_to(root).as_posix()
                if p.is_file() and _in_scope(rel):
                    yield p, rel


def _changed_lines(old, new):
    """1-based numbers of the lines that differ (a rename never adds or removes a line)."""
    return [i + 1 for i, (a, b) in enumerate(zip(old.split("\n"), new.split("\n"))) if a != b]


def _report_other_files(root, pairs, kind):
    """`path:line: name` of whole-word hits of the old names in tracked text files outside
    the rename's scope."""
    try:
        listed = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True,
                                check=True).stdout.decode("utf-8", "replace").splitlines()
    except (OSError, subprocess.CalledProcessError):
        return []
    tail = r"(?=[A-Z])\w*" if kind == "prefix" else r"\b"
    rx = re.compile(r"\b(?:" + "|".join(re.escape(old) for old, _ in pairs) + ")" + tail)
    hits = []
    for rel in listed:
        p = root / rel
        if (rel.startswith(SCOPE_DIRS) and _in_scope(rel)) or not p.is_file():
            continue
        try:
            text = p.read_bytes().decode("utf-8")
        except UnicodeDecodeError:
            continue  # binary
        for number, text_line in enumerate(text.split("\n"), 1):
            for m in rx.finditer(text_line):
                hits.append(f"{rel}:{number}: {m.group(0)}")
    return hits


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("key", nargs="?", help="an entry of RENAMES_STAGE_7")
    ap.add_argument("--check", action="store_true", help="only list what would change")
    ap.add_argument("--report", action="store_true", help="also list hits outside src/ test/")
    ap.add_argument("--list", action="store_true", help="print the table")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args(argv)
    root = Path(args.root)

    if args.list:
        for key, pairs, kind in RENAMES_STAGE_7:
            print(f"{key} ({kind}): " + ", ".join(f"{old} -> {new}" for old, new in pairs))
        return 0
    entry = _entry(args.key) if args.key else None
    if entry is None:
        print(f"error: no entry {args.key!r} in RENAMES_STAGE_7 (see --list)", file=sys.stderr)
        return 2
    pairs, kind = entry

    would_change, review, changed = [], [], 0
    for p, rel in _sources(root):
        data = p.read_bytes()
        text = data.decode("utf-8", "surrogateescape")
        new, hits = rename_text(text, pairs, kind)
        review += [f"{rel}:{line}: {name}" for line, name in hits]
        if new == text:
            continue
        if args.check:
            would_change += [f"{rel}:{line}" for line in _changed_lines(text, new)]
        else:
            p.write_bytes(new.encode("utf-8", "surrogateescape"))
            changed += 1

    if args.check:
        for hit in would_change:
            print(hit)
        print(f"{len(would_change)} line(s) would change")
    else:
        for hit in review:
            print(f"{hit} (comment, left for review)")
        print(f"{changed} file(s) changed, {len(review)} comment hit(s) left for review")
    if args.report:
        for hit in _report_other_files(root, pairs, kind):
            print(f"{hit} (outside src/ test/)")
    return 1 if args.check and would_change else 0


if __name__ == "__main__":
    sys.exit(main())
