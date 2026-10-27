#!/usr/bin/env python3
"""Layer / dependency check for the src/<context>/<layer>/ tree (spec 12 §5).

  python tools/check_layers.py [--strict] [--root DIR]

Reads every #include "…" and #include <…> in src/, test/ and include/, classifies the including
file and the included one (shared, platform, the root app/, <context>/<layer>, test, sdk) and
reports what the §5 table doesn't allow:
  * an include of a layer the table has no tick for (`display/domain -> opensky/domain`),
  * an SDK header (Arduino, LovyanGFX, WiFi, HTTPClient, Preferences, FreeRTOS, …) outside
    adapters / roots — in platform/ only inside #ifdef ARDUINO,
  * a quoted include that resolves from neither src/, include/, test/support/ nor (tests only)
    the test's own directory,
  * a source file outside any context and layer.
Prints `path:line: <from> -> <to> ("<include>")`, a count per pair and a total; warns only
(exit 0) unless --strict (stage 7 makes CI use it). tools/test_check_layers.py pins the rules
and the repository's layout.
"""

import argparse
import posixpath
import re
import sys
from collections import Counter, namedtuple
from pathlib import Path

Finding = namedtuple("Finding", "path line message")

# One #include directive: 1-based line, the name between the quotes / brackets, angle = <…>,
# arduino = the line sits inside an #ifdef ARDUINO (or the #else of #ifndef ARDUINO) branch.
Include = namedtuple("Include", "line name angle arduino")

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}

CONTEXTS = ("airspace", "enrichment", "opensky", "display", "admin")
LAYERS = ("domain", "app", "adapters", "assets")

# Headers in include/ (PlatformIO's include path) until stage 3 moves them into src/:
# the LGFX_Device subclass is the display adapter, the calibration is plain constants the pure
# touch mapping reads.
INCLUDE_DIR_LAYERS = {
    "include/LGFX_CYD.hpp": "display/adapters",
    "include/touch_calibration.h": "display/domain",
}

SDK = "sdk"

# Angle includes that are the ESP32 / Arduino SDK or the display library; everything else in
# <…> (C / C++ standard library, ArduinoJson, unity) is allowed everywhere.
_SDK_EXACT = {"Arduino.h", "HTTPClient.h", "Preferences.h", "WebServer.h", "ESPmDNS.h",
              "Update.h", "LovyanGFX.hpp"}
_SDK_PREFIXES = ("lgfx/", "esp_", "freertos/", "mbedtls/")


def is_sdk_header(name):
    """True for an SDK / LovyanGFX header name (`WiFi*.h`, `esp_*`, `freertos/…`, …)."""
    if name in _SDK_EXACT or name.startswith(_SDK_PREFIXES):
        return True
    return name.startswith("WiFi") and name.endswith(".h")


_DOMAINS = {f"{c}/domain" for c in CONTEXTS}
_APPS = {f"{c}/app" for c in CONTEXTS}

# The §5 table: {row: layers that row may include}. "Own" entries of the */app and */adapters
# rows are added by allowed_targets(). Footnotes ¹ ² (only certain types) can't be told apart
# from an include line, so they count as allowed. Rows §5 doesn't have: enrichment/app
# implements airspace's EnrichmentPort (§4.2), opensky/app sees its own context, tests see
# everything pure (not adapters, the roots or the SDK).
ALLOWED = {
    "shared": {"shared"},
    "airspace/domain": {"shared", "airspace/domain"},
    "enrichment/domain": {"shared", "enrichment/domain"},
    "opensky/domain": {"shared", "airspace/domain", "opensky/domain"},
    "display/domain": {"shared", "airspace/domain", "enrichment/domain", "display/domain",
                       "admin/domain"},
    "admin/domain": {"shared", "airspace/domain", "opensky/domain", "display/domain",
                     "admin/domain"},
    "airspace/app": {"shared", "airspace/domain", "enrichment/domain"},
    "enrichment/app": {"shared", "enrichment/domain", "airspace/domain", "airspace/app"},
    "opensky/app": {"shared", "opensky/domain"},
    "display/app": {"shared", "airspace/domain", "enrichment/domain", "display/domain",
                    "admin/domain", "platform"},
    "admin/app": {"shared", "airspace/domain", "opensky/domain", "display/domain",
                  "admin/domain"},
    "platform": {"shared", "platform"} | _DOMAINS,
    "test": {"shared", "platform", "test"} | _DOMAINS | _APPS,
}


def allowed_targets(layer):
    """Layers `layer` may include (sdk included where the table allows it unconditionally)."""
    if layer == "app":
        return None  # the composition roots may include anything
    ctx, _, kind = layer.partition("/")
    if kind == "adapters":
        return {"shared", "platform", SDK, f"{ctx}/domain", f"{ctx}/app", f"{ctx}/adapters"}
    allowed = set(ALLOWED.get(layer, set()))
    if kind == "app":
        allowed.add(layer)
    return allowed


def format_finding(f):
    return f"{f.path}:{f.line}: {f.message}"


def classify(path):
    """Layer of a repo-relative path ('shared', 'platform', 'app', '<ctx>/<layer>', 'test') or
    None when it sits outside any context and layer. Subfolders collapse into their layer
    (`display/domain/screens/x.h` -> `display/domain`)."""
    path = path.replace("\\", "/")
    if path in INCLUDE_DIR_LAYERS:
        return INCLUDE_DIR_LAYERS[path]
    parts = path.split("/")
    if parts[0] == "test" and len(parts) > 1:
        return "test"
    if parts[0] != "src" or len(parts) < 3:
        return None
    if parts[1] in ("shared", "platform", "app"):
        return parts[1]
    if parts[1] in CONTEXTS and len(parts) >= 4 and parts[2] in LAYERS:
        return f"{parts[1]}/{parts[2]}"
    return None


# ---- #include parsing -----------------------------------------------------------------------

_DIRECTIVE_RE = re.compile(r"^\s*#\s*(\w+)(.*)$")
_INCLUDE_RE = re.compile(r'^\s*(?:"([^"]+)"|<([^>]+)>)')
_ARDUINO_RE = re.compile(r"^(?:defined\s*\(\s*ARDUINO\s*\)|defined\s+ARDUINO)$")
_NOT_ARDUINO_RE = re.compile(r"^!\s*(?:defined\s*\(\s*ARDUINO\s*\)|defined\s+ARDUINO)$")


def _ident_char(ch):
    return ch.isalnum() or ch == "_"


def _code_line_starts(text):
    """Set of 1-based line numbers that start outside a comment / string — the only lines a
    preprocessor directive can be on. A small lexer like check_comments.extract_comments():
    skips // and /* */ comments, "strings", 'chars' and R"d(raw strings)d"."""
    starts = {1}
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            starts.add(line)
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not _ident_char(text[i - 1])
                                                        or text[i - 1] in "u8LU"):
            open_paren = text.find("(", i + 2)
            if open_paren < 0:
                i += 2
                continue
            delim = text[i + 2:open_paren]
            end = text.find(")" + delim + '"', open_paren)
            end = n if end < 0 else end + len(delim) + 2
            line += text.count("\n", i, end)
            i = end
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):
            i += 1
            while i < n and text[i] != c and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            if i < n and text[i] == c:
                i += 1
        else:
            i += 1
    return starts


def _condition_kind(directive, rest):
    """'ard' (#ifdef ARDUINO, #if defined(ARDUINO)), 'not' (#ifndef ARDUINO,
    #if !defined(ARDUINO)) or 'other' for an opening conditional."""
    cond = rest.split("//")[0].split("/*")[0].strip()
    if directive == "ifdef":
        return "ard" if cond == "ARDUINO" else "other"
    if directive == "ifndef":
        return "not" if cond == "ARDUINO" else "other"
    if _ARDUINO_RE.match(cond):
        return "ard"
    if _NOT_ARDUINO_RE.match(cond):
        return "not"
    return "other"


def find_includes(text):
    """[Include] for every #include directive in text, with whether it sits in an ARDUINO-only
    branch. Conditionals nest on a stack; an unrelated #if inside #ifdef ARDUINO keeps the
    state, a stray #endif or an unterminated #ifdef is tolerated."""
    code_lines = _code_line_starts(text)
    out = []
    stack = []  # [(kind, arduino state outside this conditional)]
    arduino = False
    for n, raw in enumerate(text.splitlines(), 1):
        if n not in code_lines:
            continue
        m = _DIRECTIVE_RE.match(raw)
        if not m:
            continue
        directive, rest = m.group(1), m.group(2)
        if directive in ("if", "ifdef", "ifndef"):
            kind = _condition_kind(directive, rest)
            stack.append((kind, arduino))
            arduino = {"ard": True, "not": False}.get(kind, arduino)
        elif directive in ("else", "elif"):
            if stack:
                kind, outer = stack[-1]
                if kind == "ard":
                    arduino = False
                elif kind == "not":
                    arduino = directive == "else"
                else:
                    arduino = outer
        elif directive == "endif":
            if stack:
                arduino = stack.pop()[1]
        elif directive == "include":
            inc = _INCLUDE_RE.match(rest)
            if inc:
                angle = inc.group(2) is not None
                out.append(Include(n, inc.group(2) if angle else inc.group(1), angle, arduino))
    return out


def resolve_include(path, name, known):
    """Repo-relative path the quoted include `name` in file `path` resolves to (from src/,
    include/, test/support/ or — for test files — the test's own directory), or None."""
    path = path.replace("\\", "/")
    candidates = [posixpath.normpath(posixpath.join(d, name))
                  for d in ("src", "include", "test/support")]
    if path.startswith("test/"):
        candidates.append(posixpath.normpath(posixpath.join(posixpath.dirname(path), name)))
    for c in candidates:
        if c in known:
            return c
    return None


def _check_one(path, text, known):
    layer = classify(path)
    if layer is None:
        return [Finding(path, 1, "outside any context and layer (spec 12 §4)")]
    allowed = allowed_targets(layer)
    found = []
    for inc in find_includes(text):
        shown = f"<{inc.name}>" if inc.angle else f'"{inc.name}"'
        if inc.angle:
            if not is_sdk_header(inc.name):
                continue
            target = SDK
            ok = allowed is None or SDK in allowed or (layer == "platform" and inc.arduino)
            why = " outside #ifdef ARDUINO" if layer == "platform" else ""
        else:
            resolved = resolve_include(path, inc.name, known)
            if resolved is None:
                found.append(Finding(path, inc.line, f"{layer} -> ? ({shown}) doesn't resolve "
                                     "from src/, include/ or test/support/"))
                continue
            target = classify(resolved)
            if target is None:
                continue  # reported on its own as outside any context
            ok = allowed is None or target in allowed
            why = ""
        if not ok:
            found.append(Finding(path, inc.line, f"{layer} -> {target} ({shown}){why}"))
    return found


def check_files(files):
    """[Finding] for {relpath: text}: layer violations, SDK includes, unresolved includes,
    files outside a context. Only targets present in `files` resolve."""
    known = set(files)
    found = []
    for path in sorted(files):
        found += _check_one(path, files[path], known)
    return found


def full_path_findings(files):
    """[Finding] for quoted includes in src/ that resolve only from the including file's own
    directory (the short form the full-path rule replaced)."""
    known = set(files)
    found = []
    for path in sorted(files):
        if not path.startswith("src/"):
            continue
        for inc in find_includes(files[path]):
            if inc.angle or resolve_include(path, inc.name, known) is not None:
                continue
            local = posixpath.normpath(posixpath.join(posixpath.dirname(path), inc.name))
            if local in known:
                full = local[len("src/"):]
                found.append(Finding(path, inc.line,
                                     f'short include "{inc.name}" -> write "{full}"'))
    return found


def load_tree(root, dirs=("src", "test", "include")):
    """{posix relpath: text} of the C/C++ source files under root/dirs."""
    root = Path(root)
    files = {}
    for d in dirs:
        if not (root / d).is_dir():
            continue
        for p in sorted((root / d).rglob("*")):
            if p.is_file() and p.suffix in SOURCE_SUFFIXES:
                files[p.relative_to(root).as_posix()] = p.read_text(encoding="utf-8",
                                                                     errors="replace")
    return files


def _pair(f):
    return f.message.split(" (", 1)[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--strict", action="store_true", help="exit 1 when anything is found")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args(argv)

    files = load_tree(args.root)
    findings = check_files(files) + full_path_findings(files)
    for f in findings:
        print(format_finding(f))
    if findings:
        print("per pair:")
        for pair, count in sorted(Counter(_pair(f) for f in findings).items()):
            print(f"  {count:4d}  {pair}")
    print(f"{len(findings)} layer finding(s) in total"
          + ("" if args.strict or not findings else " (report only)"))
    return 1 if args.strict and findings else 0


if __name__ == "__main__":
    sys.exit(main())
