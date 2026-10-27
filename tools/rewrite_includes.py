#!/usr/bin/env python3
"""Move the flat src/ files into src/<context>/<layer>/ and give every #include a full path
from src/ (spec 12, stage 1).

  python tools/rewrite_includes.py [--move] [--check] [--root DIR]

  --move    `git mv` every file in MOVES that is still at its old path (creates the folders,
            skips the ones already moved). Nothing else.
  (default) rewrite, in src/ and test/, every #include "X" whose X isn't a full path from src/
            to the full path of the src/ header with X's file name. <…> includes, headers from
            test/support/ or include/ and test-local headers have no src/ header of that name
            and stay as they are. Line endings and trailing comments are kept.
  --check   only report: exit 1 if the rewrite would change a file, write nothing.

Two src/ headers with the same file name make the short form ambiguous: the script stops with
both paths and writes nothing. Stages 2-3 move files again — give MOVES their table and rerun.
"""

import argparse
import posixpath
import re
import subprocess
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}
HEADER_SUFFIXES = {".h", ".hpp"}

# {path relative to src/ today: path relative to src/ after stage 1}. A file lands where its
# pure half will live (the layer native compiles); files that are entirely hardware go to
# adapters/ or the root app/ (spec 12 plan, "Placement rule for stage 1").
MOVES = {}


def _add(folder, *names):
    for name in names:
        MOVES[name] = posixpath.join(folder, posixpath.basename(name))


def _pair(name):
    return (name + ".h", name + ".cpp")


_add("shared", *_pair("sun_times"))
_add("platform", *_pair("core_link"), *_pair("perf_stats"), "net_config.h",
     *_pair("wifi_manager"), *_pair("time_sync"))
_add("airspace/domain", *_pair("flight_phase"), *_pair("cpa_predictor"))
_add("airspace/app", *_pair("poll_pipeline"))
_add("enrichment/domain", *_pair("aircraft_lookup"), *_pair("route_lookup"), "lookup_cache.h",
     *_pair("lookup_budget"), *_pair("aircraft_names"), *_pair("place_names"),
     *_pair("lookup_store"))
_add("enrichment/adapters", *_pair("hexdb_http"))
_add("opensky/domain", *_pair("opensky_client"), *_pair("opensky_auth"),
     *_pair("opensky_quota"))
_add("display/domain/screens", *_pair("table_view"), *_pair("featured_panel"),
     *_pair("flight_screen"), *_pair("radar_view"), *_pair("aircraft_summary"),
     *_pair("status_bar"), *_pair("service_screen"), *_pair("radar_geometry"))
_add("display/domain/theme", *_pair("lcars_theme"))
_add("display/domain/theme/fonts", "fonts/lcars_efont.c", "fonts/lcars_efont.h")
_add("display/domain", *_pair("screen_nav"), *_pair("redraw_policy"), *_pair("i18n"),
     *_pair("touch_input"), *_pair("backlight"))
_add("display/app", *_pair("screenshot"))
_add("admin/domain", *_pair("config_store"), *_pair("config_backup"), *_pair("web_auth"),
     *_pair("device_alerts"))
_add("admin/domain/firmware", *_pair("ota_update"), *_pair("release_check"), *_pair("version"))
_add("admin/app", *_pair("config_page"), *_pair("web_i18n"), *_pair("config_portal"))
_add("admin/adapters", "github_ca.h")
_add("app", "main.cpp", *_pair("net_task"))

_QUOTED_INCLUDE_RE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*")([^"\r\n]+)(")', re.M)


def build_index(src_paths):
    """{basename: src-relative path} of the headers in src_paths; ValueError naming both paths
    on a duplicate basename."""
    index = {}
    for path in sorted(p.replace("\\", "/") for p in src_paths):
        if Path(path).suffix not in HEADER_SUFFIXES:
            continue
        name = posixpath.basename(path)
        if name in index:
            raise ValueError(f'two headers named "{name}" in src/: {index[name]} and {path} '
                             "- a short include can't tell them apart")
        index[name] = path
    return index


def rewrite_text(text, index):
    """text with every #include "X" that doesn't resolve from src/ rewritten to index's full
    path. Includes with no src/ header of that file name are left alone."""
    full_paths = set(index.values())

    def fix(m):
        name = m.group(2)
        if name in full_paths:
            return m.group(0)
        full = index.get(posixpath.basename(name))
        return m.group(0) if full is None else m.group(1) + full + m.group(3)

    return _QUOTED_INCLUDE_RE.sub(fix, text)


def _sources(root, dirs):
    for d in dirs:
        base = root / d
        if base.is_dir():
            yield from (p for p in sorted(base.rglob("*"))
                        if p.is_file() and p.suffix in SOURCE_SUFFIXES)


def move_files(root):
    """`git mv` every MOVES entry still at its old path; returns the number moved."""
    src = root / "src"
    moved = 0
    for old, new in MOVES.items():
        if not (src / old).exists():
            continue
        (src / new).parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "mv", f"src/{old}", f"src/{new}"], cwd=root, check=True)
        moved += 1
    for d in sorted({(src / old).parent for old in MOVES}, reverse=True):
        if d != src and d.is_dir() and not any(d.iterdir()):
            d.rmdir()
    return moved


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--move", action="store_true", help="git mv the files in MOVES")
    ap.add_argument("--check", action="store_true", help="exit 1 if anything would change")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args(argv)
    root = Path(args.root)

    if args.move:
        print(f"moved {move_files(root)} file(s)")
        return 0

    try:
        index = build_index(p.relative_to(root / "src").as_posix()
                            for p in _sources(root, ("src",)))
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    changed = []
    for p in _sources(root, ("src", "test")):
        data = p.read_bytes()
        text = data.decode("utf-8", "surrogateescape")
        new = rewrite_text(text, index)
        if new != text:
            changed.append(p.relative_to(root).as_posix())
            if not args.check:
                p.write_bytes(new.encode("utf-8", "surrogateescape"))
    for path in changed:
        print(("would rewrite " if args.check else "rewrote ") + path)
    print(f"{len(changed)} file(s) " + ("would change" if args.check else "changed"))
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
