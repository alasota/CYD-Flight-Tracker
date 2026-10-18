#!/usr/bin/env python3
"""Assemble the GitHub Pages site into one folder.

    python tools/build_site.py --out _site [--firmware DIR [--boot-app0 PATH]]

Copies site/ (the page itself) plus the README's images (docs/screenshots,
docs/icons), and fills the page's "What's next" section from ROADMAP.md (in
place of the <!-- ROADMAP --> marker in site/index.html) and the current
version + changelog from git (<!-- VERSION --> / <!-- CHANGELOG -->): one entry
per v* tag, listing the commit subjects since the previous tag — so it needs a
checkout with full history and tags. With --firmware it also adds firmware/ for the in-browser
installer (ESP Web Tools): the four flash parts and a manifest.json naming the
version.

DIR holds version.txt, bootloader.bin, partitions.bin and
cyd-sky-tracker-<version>.bin, plus boot_app0.bin (or pass --boot-app0) —
that is the layout of the firmware workflow's output / a GitHub Release, and
.pio/build/cyd after `pio run` (tools/pio_version.py writes version.txt and the
versioned .bin there; boot_app0.bin then comes from the Arduino framework).

The manifest lists the parts separately rather than the merged -full.bin on
purpose: the merged image also covers the NVS partition (0x9000), so it would
wipe WiFi / home location / OpenSky settings on every update. With separate
parts NVS is only erased when the user ticks "erase device" (first install).
"""

import argparse
import html
import json
import os
import re
import shutil
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "cyd-sky-tracker"
ROADMAP = os.path.join(REPO, "ROADMAP.md")
ROADMAP_MARKER = "<!-- ROADMAP -->"
VERSION_MARKER = "<!-- VERSION -->"
CHANGELOG_MARKER = "<!-- CHANGELOG -->"
GITHUB = "https://github.com/alasota/CYD-Flight-Tracker"
CHANGELOG_MAX_ITEMS = 15  # per version; longer lists end with a link to GitHub
# Section headings in ROADMAP.md -> Polish.
SECTION_PL = {"Planned": "Planowane", "In progress": "W trakcie", "Done": "Zrobione"}
# Roadmap table: column headers, and the rating words per column (Polish
# adjectives agree with the column's noun: wartość / wysiłek / ryzyko).
COLUMN_PL = {"Item": "Pozycja", "Value": "Wartość", "Effort": "Wysiłek", "Risk": "Ryzyko",
             "Rating": "Ocena", "Verdict": "Werdykt"}
WORDS_PL = {
    "Value": {"high": "wysoka", "medium": "średnia", "low": "niska"},
    "Effort": {"small": "mały", "medium": "średni", "large": "duży"},
    "Risk": {"low": "niskie", "medium": "średnie", "high": "wysokie"},
}
PL_COMMENT = re.compile(r"<!--\s*pl:\s*(.*?)\s*-->")


def split_cell(cell):
    """(english, polish) of a table cell: text + optional <!-- pl: ... --> comment."""
    m = PL_COMMENT.search(cell)
    en = PL_COMMENT.sub("", cell).strip()
    return en, (m.group(1).strip() if m else en)


def translate_words(column, text):
    """'medium–large' -> 'średni–duży' for the rating columns; anything else as is."""
    words = WORDS_PL.get(column, {})
    return re.sub(r"[A-Za-z]+", lambda w: words.get(w.group(0).lower(), w.group(0)), text)


def parse_roadmap(text):
    """[(section, columns, [row, ...]), ...] from ROADMAP.md, rows in file order;
    each row is a list of (english, polish) cells."""
    # Drop the multi-line format notes (single-line pl: comments stay).
    text = re.sub(r"^<!--\s*\n.*?-->\s*$", "", text, flags=re.S | re.M)
    sections = []
    for line in text.splitlines():
        heading = re.match(r"^##\s+(.+?)\s*$", line)
        if heading:
            sections.append((heading.group(1), None, []))
            continue
        if not sections or not line.strip().startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if all(re.fullmatch(r":?-+:?", c) for c in cells):
            continue  # |---|---| separator
        name, columns, rows = sections[-1]
        if columns is None:
            sections[-1] = (name, cells, rows)
            continue
        row = []
        for col, cell in zip(columns, cells):
            en, pl = split_cell(cell)
            row.append((en, translate_words(col, pl)))
        rows.append(row)
    return [s for s in sections if s[1] and s[2]]


def roadmap_html(sections):
    def both(en, pl):
        if en == pl:
            return html.escape(en)
        return '<span class="en">%s</span><span class="pl">%s</span>' % (
            html.escape(en), html.escape(pl))

    blocks = []
    for name, columns, rows in sections:
        head = "".join('<th class="c-%s">%s</th>' % (
            re.sub(r"\W", "", c.lower()) or "n", both(c, COLUMN_PL.get(c, c))) for c in columns)
        body = "".join("<tr>%s</tr>" % "".join(
            '<td class="c-%s">%s</td>' % (re.sub(r"\W", "", c.lower()) or "n", both(en, pl))
            for c, (en, pl) in zip(columns, row)) for row in rows)
        blocks.append('<div class="card"><h3>%s</h3><div class="table-scroll">'
                      '<table class="roadmap-table"><thead><tr>%s</tr></thead>'
                      '<tbody>%s</tbody></table></div></div>' % (
                          both(name, SECTION_PL.get(name, name)), head, body))
    return '<div class="roadmap">%s</div>' % "".join(blocks)


def git(*args):
    try:
        return subprocess.check_output(["git"] + list(args), cwd=REPO,
                                       stderr=subprocess.DEVNULL).decode("utf-8").strip()
    except Exception:
        return ""


def releases():
    """[(tag, date, [commit subjects]), ...], newest first: one entry per v* tag,
    with the non-merge commits since the previous tag (all history for the first)."""
    tags = [t for t in git("tag", "--list", "v*", "--sort=-v:refname").splitlines() if t]
    out = []
    for i, tag in enumerate(tags):
        rng = tag if i == len(tags) - 1 else "%s..%s" % (tags[i + 1], tag)
        subjects = [s for s in git("log", "--no-merges", "--format=%s", rng).splitlines() if s]
        out.append((tag, git("log", "-1", "--format=%cs", tag), subjects))
    return out


def both(en, pl):
    return '<span class="en">%s</span><span class="pl">%s</span>' % (en, pl)


def version_html(rels):
    if not rels:
        return ""
    tag, date, _ = rels[0]
    return '<a class="version-badge" href="#changelog">%s <b>%s</b> · %s</a>' % (
        both("Current version", "Aktualna wersja"), html.escape(tag), html.escape(date))


def changelog_html(rels):
    if not rels:
        return '<p class="muted center">%s</p>' % both("No releases yet.", "Jeszcze nic nie wydano.")
    blocks = []
    for i, (tag, date, subjects) in enumerate(rels):
        prev = rels[i + 1][0] if i + 1 < len(rels) else None
        items = "".join("<li>%s</li>" % html.escape(s) for s in subjects[:CHANGELOG_MAX_ITEMS])
        more = len(subjects) - CHANGELOG_MAX_ITEMS
        if more > 0:
            url = "%s/compare/%s...%s" % (GITHUB, prev, tag) if prev else "%s/commits/%s" % (GITHUB, tag)
            items += '<li class="more"><a href="%s">%s</a></li>' % (
                url, both("…and %d more" % more, "…i jeszcze %d" % more))
        links = '<a href="%s/releases/tag/%s">%s</a>' % (GITHUB, tag, both("release", "wydanie"))
        if prev:
            links += ' · <a href="%s/compare/%s...%s">%s</a>' % (
                GITHUB, prev, tag, both("diff", "różnice"))
        blocks.append(
            '<details class="card"%s><summary><b>%s</b> <span class="muted">%s · %s</span>'
            '<span class="links-r">%s</span></summary><ul class="ticks">%s</ul></details>' % (
                " open" if i == 0 else "", html.escape(tag), html.escape(date),
                both("%d change(s)" % len(subjects), "zmian: %d" % len(subjects)), links,
                items or "<li>—</li>"))
    return '<div class="changelog">%s</div>' % "".join(blocks)


def fill_releases(out):
    index = os.path.join(out, "index.html")
    with open(index, encoding="utf-8") as f:
        page = f.read()
    for marker in (VERSION_MARKER, CHANGELOG_MARKER):
        if marker not in page:
            raise SystemExit("index.html has no %s marker" % marker)
    rels = releases()
    page = page.replace(VERSION_MARKER, version_html(rels))
    page = page.replace(CHANGELOG_MARKER, changelog_html(rels))
    with open(index, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print("changelog: %d release(s)%s" % (len(rels), ", current " + rels[0][0] if rels else ""))


def fill_roadmap(out):
    index = os.path.join(out, "index.html")
    with open(index, encoding="utf-8") as f:
        page = f.read()
    if ROADMAP_MARKER not in page:
        raise SystemExit("index.html has no %s marker" % ROADMAP_MARKER)
    with open(ROADMAP, encoding="utf-8") as f:
        sections = parse_roadmap(f.read())
    with open(index, "w", encoding="utf-8", newline="\n") as f:
        f.write(page.replace(ROADMAP_MARKER, roadmap_html(sections)))
    print("roadmap: %d item(s)" % sum(len(rows) for _, _, rows in sections))


def copy_tree(src, dst):
    if os.path.isdir(src):
        shutil.copytree(src, dst, dirs_exist_ok=True)


def add_firmware(out, src_dir, boot_app0):
    with open(os.path.join(src_dir, "version.txt")) as f:
        version = f.read().strip()
    fw = os.path.join(out, "firmware")
    os.makedirs(fw, exist_ok=True)

    app = "%s-%s.bin" % (NAME, version)
    boot_app0 = boot_app0 or os.path.join(src_dir, "boot_app0.bin")
    parts = [
        ("bootloader.bin", os.path.join(src_dir, "bootloader.bin"), 0x1000),
        ("partitions.bin", os.path.join(src_dir, "partitions.bin"), 0x8000),
        ("boot_app0.bin", boot_app0, 0xE000),
        (app, os.path.join(src_dir, app), 0x10000),
    ]
    for name, src, _ in parts:
        if not os.path.exists(src):
            raise SystemExit("missing firmware part: " + src)
    for name, src, _ in parts:
        shutil.copyfile(src, os.path.join(fw, name))

    manifest = {
        "name": "CYD Sky Tracker",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,  # the firmware doesn't speak Improv
        "builds": [{
            "chipFamily": "ESP32",
            "parts": [{"path": name, "offset": offset} for name, _, offset in parts],
        }],
    }
    with open(os.path.join(fw, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    return version


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--firmware", help="folder with the firmware files (see above)")
    ap.add_argument("--boot-app0", help="boot_app0.bin, if not in the --firmware folder")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    copy_tree(os.path.join(REPO, "site"), out)
    copy_tree(os.path.join(REPO, "docs", "screenshots"), os.path.join(out, "screenshots"))
    copy_tree(os.path.join(REPO, "docs", "icons"), os.path.join(out, "icons"))
    fill_roadmap(out)
    fill_releases(out)

    if args.firmware:
        print("firmware", add_firmware(out, args.firmware, args.boot_app0))
    else:
        print("no firmware: the page will point to Releases instead of the installer")
    print("site assembled in", out)


if __name__ == "__main__":
    main()
