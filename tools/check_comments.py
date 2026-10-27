#!/usr/bin/env python3
"""Comment / docs hygiene check (spec 11).

  python tools/check_comments.py [--strict] [--root DIR]

Finds, in src/ and test/:
  * symbols removed in spec 11 (dead code that must not come back) — in code or comments,
  * comments in network-core modules (net_task, core 0) that credit main.cpp / loopTask,
  * doc references (`CLAUDE.md "X"`, `CLAUDE.md's "X"`, `docs/<f>.md "X"`) that don't hit a
    heading of that file (any level; the heading has to *start with* X),
  * the old `CLAUDE.md review notes X.Y` form (the notes are docs/review-notes-2026.md now),
and checks that CLAUDE.md stays under 250 lines. Prints `path:line: message`; warns only
(exit 0) unless --strict. tools/test_check_comments.py pins the repository's clean state.
"""

import argparse
import re
import sys
from collections import namedtuple
from pathlib import Path

Finding = namedtuple("Finding", "path line message")


def format_finding(f):
    return f"{f.path}:{f.line}: {f.message}"


# Removed in spec 11 (§1 + the owner's extras). rssiToBars never existed in code — it was only
# described; a mention of it as a function is just as wrong as the code coming back.
REMOVED_SYMBOLS = [
    "viewToggleButtonBounds", "kToggleButtonWidth", "kToggleButtonHeight",
    "kToggleButtonMargin", "drawViewToggleButton",
    "drawElbowFrame", "drawElbow", "elbowArcPoint", "ElbowArcPoint",
    "drawVerticalDivider", "drawPanel", "kPanelCornerRadius", "drawHeaderBlock",
    "phaseIcon", "formatSummaryIdentity",
    "LCARS_MAGENTA", "LCARS_CYAN", "LCARS_AMBER", "LCARS_ROSE", "LCARS_PALE_BLUE",
    "LCARS_BLUE_VIOLET", "LCARS_FONT_NUMERIC",
    "RefreshNow", "rssiToBars",
]

# Modules (file stems in src/) whose code runs in net_task on core 0. A comment in one of them
# that says main.cpp / loopTask does the work is describing the pre-spec-04 single loop().
NET_MODULES = {
    "net_task", "wifi_manager", "config_portal", "config_store", "opensky_client",
    "opensky_auth", "opensky_quota", "aircraft_lookup", "route_lookup", "lookup_store",
    "lookup_budget", "lookup_cache", "hexdb_http", "poll_pipeline", "release_check",
    "ota_update",
}

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}

_SYMBOL_RE = re.compile(r"\b(" + "|".join(sorted(REMOVED_SYMBOLS, key=len, reverse=True))
                        + r")\b")
_MAIN_CPP_RE = re.compile(r"\bmain\.cpp\b|\bloopTask\b")
_DOC_REF_RE = re.compile(r"\b(CLAUDE\.md|docs/[\w.-]+\.md)(?:'s)? \"([^\"]+)\"")
_OLD_REVIEW_NOTES_RE = re.compile(r"\bCLAUDE\.md(?:'s)? review notes\b")
_HEADING_RE = re.compile(r"^#{1,6}\s+(.*?)\s*#*\s*$")


def extract_comments(text):
    """[(line, comment text)] for every // and /* */ comment line (1-based lines).

    A small C/C++ lexer: skips "strings" (with escapes), 'chars' and R"d(raw strings)d", so
    `"http://x"` is not a comment. A block comment gives one entry per line it spans. The
    text is what follows `//` / `/*` (or the line's part inside the block), unstripped.
    """
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append((line, text[i + 2:end]))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end
            for k, part in enumerate(text[i + 2:end].split("\n")):
                out.append((line + k, part))
            line += text.count("\n", i, end)
            i = end + 2
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not _ident_char(text[i - 1])
                                                        or text[i - 1] in "u8LU"):
            open_paren = text.find("(", i + 2)
            delim = text[i + 2:open_paren]
            end = text.find(")" + delim + '"', open_paren)
            end = n if end < 0 else end + len(delim) + 2
            line += text.count("\n", i, end)
            i = end
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            i += 1
            while i < n and text[i] != c and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            i += 1
    return out


def _ident_char(ch):
    return ch.isalnum() or ch == "_"


def _comment_blocks(text):
    """Runs of comments on consecutive lines, joined with one space: [(joined, [(offset,
    line)])] — so a reference split over two comment lines (`see CLAUDE.md` / `"Testing"`)
    is still one reference."""
    blocks = []
    for line, body in extract_comments(text):
        part = body.strip().lstrip("*/!<").strip()
        if blocks and blocks[-1][1][-1][1] == line - 1:
            joined, starts = blocks[-1]
            starts.append((len(joined) + 1, line))
            blocks[-1] = (joined + " " + part, starts)
        else:
            blocks.append((part, [(0, line)]))
    return blocks


def _line_at(starts, offset):
    line = starts[0][1]
    for start, ln in starts:
        if start <= offset:
            line = ln
    return line


def find_removed_symbols(files):
    """files: {relpath: text}. Findings for every removed symbol (code or comment)."""
    found = []
    for path, text in sorted(files.items()):
        for n, raw in enumerate(text.splitlines(), 1):
            for m in _SYMBOL_RE.finditer(raw):
                found.append(Finding(path, n, f"{m.group(1)} was removed in spec 11"))
    return found


def is_net_module(path):
    p = Path(path)
    return p.parts[:1] == ("src",) and p.stem in NET_MODULES


def find_main_cpp_comments(files):
    """Findings for comments mentioning main.cpp / loopTask in net modules (core 0)."""
    found = []
    for path, text in sorted(files.items()):
        if not is_net_module(path):
            continue
        for line, body in extract_comments(text):
            m = _MAIN_CPP_RE.search(body)
            if m:
                found.append(Finding(path, line, f"comment credits {m.group(0)} in a net_task "
                                     "module (core 0) — say net_task / poll_pipeline"))
    return found


def headings(markdown):
    """Heading texts of a Markdown file, any level, without the #s."""
    out = []
    in_fence = False
    for raw in markdown.splitlines():
        if raw.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        m = None if in_fence else _HEADING_RE.match(raw)
        if m:
            out.append(m.group(1))
    return out


def find_bad_doc_refs(files, docs):
    """docs: {relpath: markdown}. Findings for doc references that don't resolve."""
    heads = {name: headings(text) for name, text in docs.items()}
    found = []
    for path, text in sorted(files.items()):
        for joined, starts in _comment_blocks(text):
            for m in _OLD_REVIEW_NOTES_RE.finditer(joined):
                found.append(Finding(path, _line_at(starts, m.start()),
                                     "the review notes moved: docs/review-notes-2026.md X.Y"))
            for m in _DOC_REF_RE.finditer(joined):
                doc, section = m.group(1), m.group(2)
                line = _line_at(starts, m.start())
                if doc not in heads:
                    found.append(Finding(path, line, f'{doc} "{section}": no such file'))
                elif not any(h.startswith(section) for h in heads[doc]):
                    found.append(Finding(path, line,
                                         f'{doc} "{section}": no heading starts with it'))
    return found


def check_claude_md_size(text, limit=250):
    """[Finding] when CLAUDE.md has `limit` lines or more (a trailing newline ends the last
    line, it doesn't start a new one)."""
    lines = len(text.splitlines())
    if lines >= limit:
        return [Finding("CLAUDE.md", lines, f"CLAUDE.md has {lines} lines, keep it under "
                        f"{limit} (details go to docs/)")]
    return []


def load_tree(root, dirs=("src", "test")):
    """{posix relpath: text} of the C/C++ source files under root/dirs."""
    root = Path(root)
    files = {}
    for d in dirs:
        for p in sorted((root / d).rglob("*")):
            if p.is_file() and p.suffix in SOURCE_SUFFIXES:
                files[p.relative_to(root).as_posix()] = p.read_text(encoding="utf-8",
                                                                     errors="replace")
    return files


def load_docs(root):
    """{posix relpath: text} of CLAUDE.md and docs/*.md under root."""
    root = Path(root)
    docs = {}
    candidates = [root / "CLAUDE.md"] + sorted((root / "docs").glob("*.md"))
    for p in candidates:
        if p.is_file():
            docs[p.relative_to(root).as_posix()] = p.read_text(encoding="utf-8")
    return docs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--strict", action="store_true", help="exit 1 when anything is found")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args(argv)

    files = load_tree(args.root)
    docs = load_docs(args.root)
    findings = (find_removed_symbols(files) + find_main_cpp_comments(files)
                + find_bad_doc_refs(files, docs))
    if "CLAUDE.md" in docs:
        findings += check_claude_md_size(docs["CLAUDE.md"])
    for f in findings:
        print(format_finding(f))
    if findings:
        print(f"{len(findings)} finding(s)" + ("" if args.strict else " (warning only)"))
    return 1 if args.strict and findings else 0


if __name__ == "__main__":
    sys.exit(main())
