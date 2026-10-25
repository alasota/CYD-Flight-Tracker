#!/usr/bin/env python3
"""What the next release is: version, what goes into it, what still has to be updated.

Used by /release (.claude/skills/release/SKILL.md). Reads git tags and the pull requests
merged into main since the latest release (gh if available, else the public GitHub API),
and prints a plan as JSON:

  python tools/release_plan.py                # plan for the next release
  python tools/release_plan.py --bump minor   # force the bump (patch / minor / major)

Rules:
- The latest release = the highest vX.Y.Z tag by version (not by `git describe`: tags made
  on a PR branch before a squash merge aren't ancestors of main, v1.4.3-v1.5.1 are like that).
  Development tags (anything with a "-") never count.
- A PR from `spec/…` or `issue/…` is a feature -> minor (X.Y+1.0), unless it carries the
  `bug` label. A PR from `fix/…` or with the `bug` label is a fix -> patch (X.Y.Z+1).
  `release/…` PRs (the release's own housekeeping) are skipped. Anything else (`harness/…`,
  docs, CI) is "other": patch on its own, the owner confirms.
- Specs to move to spec/done/: spec/planned/NN-*.md whose number appears in a merged feature
  branch (`spec/NN-…`) or whose status line says it's done.
- vNEXT: docs written before the version was known carry a bare vNEXT placeholder (ROADMAP's
  Done rows, spec/README.md, spec status lines); the release replaces it with the version
  (`--fill vX.Y.Z` does it). Written in backticks it is text *about* the placeholder and stays.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
GITHUB_REPO = "alasota/CYD-Flight-Tracker"
STABLE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
PLACEHOLDER = "vNEXT"
# A placeholder is a bare vNEXT; `vNEXT` in backticks is text about it (CLAUDE.md, READMEs).
BARE_VNEXT = re.compile(r"(?<![`\w])vNEXT(?![`\w])")
# Files where vNEXT may stand for the coming version.
VNEXT_GLOBS = ["ROADMAP.md", "README.md", "CLAUDE.md", "spec/**/*.md", "site/**/*.html"]


# ---- pure ---------------------------------------------------------------------------

def parse_version(tag):
    """'v1.6.0' -> (1, 6, 0); None for anything else (dev tags, rc, garbage)."""
    m = STABLE.match(tag.strip())
    return tuple(int(x) for x in m.groups()) if m else None


def latest_release(tags):
    """Highest stable tag, or None."""
    stable = [(parse_version(t), t) for t in tags if parse_version(t)]
    return max(stable)[1] if stable else None


def next_version(latest, bump):
    major, minor, patch = parse_version(latest) if latest else (0, 0, 0)
    if bump == "major":
        return f"v{major + 1}.0.0"
    if bump == "minor":
        return f"v{major}.{minor + 1}.0"
    if bump == "patch":
        return f"v{major}.{minor}.{patch + 1}"
    raise ValueError(f"unknown bump {bump!r}")


def classify_pr(head, labels):
    """'feature' / 'fix' / 'release' / 'other' from the branch name and labels."""
    labels = {l.lower() for l in labels}
    if head.startswith("release/"):
        return "release"
    if head.startswith("fix/") or "bug" in labels:
        return "fix"
    if head.startswith(("spec/", "issue/")) or labels & {"enhancement", "feature"}:
        return "feature"
    return "other"


def choose_bump(kinds):
    """minor if anything is a feature, patch otherwise; None if there's nothing to release."""
    kinds = [k for k in kinds if k != "release"]
    if not kinds:
        return None
    return "minor" if "feature" in kinds else "patch"


def spec_number(head):
    """'spec/06-opensky-login-fallback' -> '06'; None for other branches."""
    m = re.match(r"^spec/(\d{2})-", head)
    return m.group(1) if m else None


DONE_STATUS = re.compile(r"Status:\s*\*\*(zrobione|zaimplementowane|done)", re.I)


def specs_to_move(planned_files, merged_spec_numbers, read):
    """spec/planned/NN-*.md (and its .feature) that should go to spec/done/."""
    out = []
    for path in sorted(planned_files):
        name = Path(path).name
        if not name.endswith(".md"):
            continue
        nn = name[:2]
        head = read(path)[:600]
        if nn in merged_spec_numbers or DONE_STATUS.search(head):
            out.append(path)
    return out


# ---- I/O ----------------------------------------------------------------------------

def git(*args):
    return subprocess.run(["git", *args], cwd=REPO_DIR, check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout.strip()


def find_gh():
    found = shutil.which("gh")
    if found:
        return found
    win = Path(r"C:\Program Files\GitHub CLI\gh.exe")
    return str(win) if win.exists() else None


def merged_prs_since(iso_date):
    """[{number, title, head, labels, merged_at}] merged into main after iso_date."""
    gh = find_gh()
    if gh:
        out = subprocess.run(
            [gh, "pr", "list", "--repo", GITHUB_REPO, "--state", "merged", "--base", "main",
             "--limit", "100", "--search", f"merged:>{iso_date}",
             "--json", "number,title,headRefName,labels,mergedAt,url"],
            check=True, capture_output=True, text=True, encoding="utf-8").stdout
        prs = json.loads(out)
        return [{"number": p["number"], "title": p["title"], "head": p["headRefName"],
                 "labels": [l["name"] for l in p["labels"]], "merged_at": p["mergedAt"],
                 "url": p["url"]} for p in prs]
    req = urllib.request.Request(
        f"https://api.github.com/repos/{GITHUB_REPO}/pulls?state=closed&base=main"
        "&sort=updated&direction=desc&per_page=100",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "release_plan"})
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=20) as r:
        prs = json.load(r)
    return [{"number": p["number"], "title": p["title"], "head": p["head"]["ref"],
             "labels": [l["name"] for l in p["labels"]], "merged_at": p["merged_at"],
             "url": p["html_url"]}
            for p in prs if p["merged_at"] and p["merged_at"] > iso_date]


def has_placeholder(text):
    return bool(BARE_VNEXT.search(text))


def fill_placeholder(text, version):
    """Bare vNEXT -> version; `vNEXT` in backticks (text about it) stays."""
    return BARE_VNEXT.sub(version, text)


def vnext_files():
    hits = set()
    for pattern in VNEXT_GLOBS:
        for p in REPO_DIR.glob(pattern):
            if p.is_file() and has_placeholder(p.read_text(encoding="utf-8", errors="replace")):
                hits.add(p.relative_to(REPO_DIR).as_posix())
    return sorted(hits)


def utc_iso(unix_seconds):
    """1761413412 -> '2026-10-25T17:30:12Z' — the form GitHub's merged_at uses."""
    t = datetime.datetime.fromtimestamp(int(unix_seconds), datetime.timezone.utc)
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def build_plan(bump_override=None, from_tag=None):
    tags = git("tag", "--list", "v*").splitlines()
    latest = from_tag or latest_release(tags)
    # The tagged commit's time; a PR merged at that second is already in it.
    since_utc = utc_iso(git("log", "-1", "--format=%ct", latest)) if latest else "1970-01-01T00:00:00Z"
    prs = merged_prs_since(since_utc)
    for p in prs:
        p["kind"] = classify_pr(p["head"], p["labels"])
    bump = bump_override or choose_bump([p["kind"] for p in prs])
    merged_specs = {n for n in (spec_number(p["head"]) for p in prs if p["kind"] == "feature") if n}
    planned = [p.relative_to(REPO_DIR).as_posix() for p in (REPO_DIR / "spec" / "planned").glob("*.md")]
    commits = git("log", "--format=%h %s", f"{latest}..origin/main" if latest else "origin/main")
    return {
        "latest": latest,
        "latest_date": since_utc,
        "bump": bump,
        "next": next_version(latest, bump) if bump else None,
        "prs": prs,
        "commits_since": commits.splitlines() if commits else [],
        "specs_to_move": specs_to_move(planned, merged_specs,
                                       lambda f: (REPO_DIR / f).read_text(encoding="utf-8")),
        "vnext_files": vnext_files(),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bump", choices=["patch", "minor", "major"], help="override the bump")
    ap.add_argument("--from", dest="from_tag", metavar="TAG",
                    help="plan as if TAG were the latest release (to check an old one)")
    ap.add_argument("--fill", metavar="VERSION",
                    help="replace the bare vNEXT placeholders with VERSION in place, list the files")
    args = ap.parse_args(argv)
    if args.fill:
        if not parse_version(args.fill):
            ap.error(f"--fill needs vX.Y.Z, got {args.fill!r}")
        for rel in vnext_files():
            path = REPO_DIR / rel
            raw = path.read_bytes().decode("utf-8")
            path.write_bytes(fill_placeholder(raw, args.fill).encode("utf-8"))
            print(rel)
        return 0
    print(json.dumps(build_plan(args.bump, args.from_tag), indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
