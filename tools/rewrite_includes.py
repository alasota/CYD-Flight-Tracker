#!/usr/bin/env python3
"""Move src/ files to their spec 12 folders and give every #include a full path from src/
(spec 12, stages 1, 2, 4 and 7).

  python tools/rewrite_includes.py [--move] [--check] [--root DIR]

  --move    `git mv` every file in MOVES (the newest stage's table) that is still at its old
            path (creates the folders, skips the ones already moved, removes emptied folders).
            Nothing else.
  (default) rewrite, in src/ and test/, every #include "X" whose X isn't a full path from src/
            to the full path of the src/ header with X's file name - or, for a header a stage
            renamed, with its old file name. <…> includes, headers from test/support/ or
            include/ and test-local headers have no src/ header of that name and stay as they
            are. Line endings and trailing comments are kept.
  --check   only report: exit 1 if the rewrite would change a file, write nothing.

Two src/ headers with the same file name make the short form ambiguous: the script stops with
both paths and writes nothing; so does a renamed header's old file name that another live header
still has. One move table per stage (MOVES_STAGE_1: flat src/ into <context>/<layer>/;
MOVES_STAGE_2: layers renamed to core / services, roots into main/; MOVES_STAGE_4: config_portal
renamed web_server; MOVES_STAGE_7: config_store renamed settings; MOVES_STAGE_8: screenshot
to admin/core), kept side by side so
final_path() can follow a file from stage 0 to today. A later stage that moves files adds its
table to STAGES. Stage 4 also moved two headers from include/ by hand (git mv); the default
rewrite fixed their includes.
"""

import argparse
import posixpath
import re
import subprocess
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}
HEADER_SUFFIXES = {".h", ".hpp"}

# {path relative to src/ at stage 0: path relative to src/ after stage 1}. A file lands where
# its pure half will live (the layer native compiles); files that are entirely hardware go to
# adapters/ or the root app/ (spec 12 plan, "Placement rule for stage 1").
MOVES_STAGE_1 = {}


def _add(folder, *names):
    for name in names:
        MOVES_STAGE_1[name] = posixpath.join(folder, posixpath.basename(name))


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

# {path after stage 1: path after stage 2} for the files whose path changes in stage 2: the
# layer folders are renamed (<ctx>/domain/ -> <ctx>/core/, <ctx>/app/ -> <ctx>/services/, the
# root app/ -> main/), except admin/app/, which splits: the pure page HTML and its strings go to
# admin/core/, the WebServer routes to admin/adapters/. No file changes its name.
_STAGE_2_SPLIT = {
    "admin/app/config_page.h": "admin/core/config_page.h",
    "admin/app/config_page.cpp": "admin/core/config_page.cpp",
    "admin/app/web_i18n.h": "admin/core/web_i18n.h",
    "admin/app/web_i18n.cpp": "admin/core/web_i18n.cpp",
    "admin/app/config_portal.h": "admin/adapters/config_portal.h",
    "admin/app/config_portal.cpp": "admin/adapters/config_portal.cpp",
}
_STAGE_2_LAYER_RE = re.compile(r"^(?:(\w+)/(domain|app)|(app))/")


def _stage_2_path(path):
    """path's stage-2 location, or None if stage 2 doesn't move it."""
    if path in _STAGE_2_SPLIT:
        return _STAGE_2_SPLIT[path]
    m = _STAGE_2_LAYER_RE.match(path)
    if m is None:
        return None
    if m.group(3):
        return "main/" + path[m.end():]
    layer = "core" if m.group(2) == "domain" else "services"
    return f"{m.group(1)}/{layer}/" + path[m.end():]


MOVES_STAGE_2 = {mid: new for mid in MOVES_STAGE_1.values()
                 if (new := _stage_2_path(mid)) is not None}

# Stage 4 moves no file between folders (its splits write new files by hand); it renames the
# settings page's web server adapter after what it is (spec 12 section 4.5).
MOVES_STAGE_4 = {
    "admin/adapters/config_portal.h": "admin/adapters/web_server.h",
    "admin/adapters/config_portal.cpp": "admin/adapters/web_server.cpp",
}

# Stage 7 renames the settings module after the glossary's Settings (spec 12 section 3).
MOVES_STAGE_7 = {
    "admin/core/config_store.h": "admin/core/settings.h",
    "admin/core/config_store.cpp": "admin/core/settings.cpp",
}

# Stage 8 moves the BMP / ?screen= helpers to the web panel that uses them.
MOVES_STAGE_8 = {
    "display/services/screenshot.h": "admin/core/screenshot.h",
    "display/services/screenshot.cpp": "admin/core/screenshot.cpp",
}

STAGES = (MOVES_STAGE_1, MOVES_STAGE_2, MOVES_STAGE_4, MOVES_STAGE_7, MOVES_STAGE_8)
MOVES = STAGES[-1]  # what --move applies: the newest stage


def final_path(stage0_path):
    """Path relative to src/ that the stage-0 file `stage0_path` has after the last stage."""
    path = stage0_path
    for table in STAGES:
        path = table.get(path, path)
    return path


_QUOTED_INCLUDE_RE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*")([^"\r\n]+)(")', re.M)


def _renamed_headers():
    """{old basename: path after the last stage} for every header a stage table renamed."""
    renamed = {}
    for i, table in enumerate(STAGES):
        for old, new in table.items():
            if Path(new).suffix not in HEADER_SUFFIXES:
                continue
            if posixpath.basename(old) == posixpath.basename(new):
                continue
            for later in STAGES[i + 1:]:
                new = later.get(new, new)
            renamed[posixpath.basename(old)] = new
    return renamed


def build_index(src_paths):
    """{basename: src-relative path} of the headers in src_paths, plus the old basename of
    every renamed header that exists (pointing at its new path); ValueError naming both paths
    on a duplicate basename, or on an old basename another live header still has."""
    index = {}
    for path in sorted(p.replace("\\", "/") for p in src_paths):
        if Path(path).suffix not in HEADER_SUFFIXES:
            continue
        name = posixpath.basename(path)
        if name in index:
            raise ValueError(f'two headers named "{name}" in src/: {index[name]} and {path} '
                             "- a short include can't tell them apart")
        index[name] = path
    live = set(index.values())
    for old_name, new_path in _renamed_headers().items():
        if new_path not in live:
            continue
        if old_name in index:
            raise ValueError(f'"{old_name}" was renamed to {new_path}, but {index[old_name]} '
                             "still has that name - a short include can't tell them apart")
        index[old_name] = new_path
    return index


def rewrite_text(text, index):
    """text with every #include "X" that doesn't resolve from src/ rewritten to index's full
    path (an old name of a renamed header -> its new path). Includes with no src/ header of
    that file name are left alone."""
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
