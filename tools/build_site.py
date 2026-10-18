#!/usr/bin/env python3
"""Assemble the GitHub Pages site into one folder.

    python tools/build_site.py --out _site [--firmware DIR [--boot-app0 PATH]]

Copies site/ (the page itself) plus the README's images (docs/screenshots,
docs/icons). With --firmware it also adds firmware/ for the in-browser
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
import json
import os
import shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "cyd-sky-tracker"


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
    # Pages would otherwise run Jekyll over it.
    open(os.path.join(out, ".nojekyll"), "w").close()

    if args.firmware:
        print("firmware", add_firmware(out, args.firmware, args.boot_app0))
    else:
        print("no firmware: the page will point to Releases instead of the installer")
    print("site assembled in", out)


if __name__ == "__main__":
    main()
