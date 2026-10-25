#!/usr/bin/env python3
"""Screenshots of the config page and the update question, made from the same demo
data the tests use (test_admin/config_page_test.cpp; release_check's updateDemoState()):

  docs/screenshots/web_general.png       config page, General tab: the Display card
                                         (brightness + night mode, spec/03)
  docs/screenshots/web_opensky.png       config page, OpenSky tab: credits/day with
                                         the night pause counted
  docs/screenshots/web_firmware.png      config page, Firmware tab: v1.3.0 available
                                         (spec/02-github-release-check.md)
  docs/screenshots/web_firmware_dev.png  ...with "show development versions" on
  docs/screenshots/update_prompt.png     the question on the display (--device)

The web pages need no board: `pio test -e native -f test_admin` renders them
(SCREENSHOT_OUT set) and headless Chrome / Edge photographs them at 500 px x2, like
the other web_*.png. The display one is drawn by the board itself
(GET /screenshot.bmp?screen=3 — the prompt with the demo data, then back to the
normal screen); LovyanGFX can't run on the PC without SDL.

  python tools/screenshots.py                          # web pages only
  python tools/screenshots.py --device cyd-sky.local   # + the display prompt
  (panel password: --password or $CYD_WEB_PASSWORD; user "admin")

Needs: Pillow (pip install pillow), Chrome or Edge, PlatformIO.
"""
import argparse
import io
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "docs" / "screenshots"
PAGE_BG = (0x17, 0x18, 0x1C)  # config_page's --bg
UPDATE_PROMPT_SCREEN = 3  # main.cpp: kScreenshotUpdatePrompt = kScreenCount


def find_pio():
    for cand in (shutil.which("pio"), shutil.which("platformio"),
                 Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
                 Path.home() / ".platformio" / "penv" / "bin" / "pio"):
        if cand and Path(cand).exists():
            return str(cand)
    sys.exit("PlatformIO (pio) not found")


def find_browser():
    for cand in (shutil.which("chrome"), shutil.which("google-chrome"), shutil.which("chromium"),
                 shutil.which("msedge"),
                 r"C:\Program Files\Google\Chrome\Application\chrome.exe",
                 r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"):
        if cand and Path(cand).exists():
            return str(cand)
    sys.exit("Chrome / Edge not found")


def render_pages(tmp):
    env = dict(os.environ, SCREENSHOT_OUT=str(tmp))
    # Windows' application control now and then blocks a freshly built test
    # program (WinError 4551) — a second run gets through.
    for _ in range(3):
        r = subprocess.run([find_pio(), "test", "-e", "native", "-f", "test_admin"],
                           cwd=REPO, env=env, capture_output=True, encoding="utf-8",
                           errors="replace")
        if r.returncode == 0 or "4551" not in r.stdout:
            break
    if r.returncode != 0:
        sys.exit("test_admin failed:\n" + r.stdout[-3000:] + r.stderr[-2000:])
    return [tmp / name for name in ("web_general.html", "web_opensky.html",
                                    "web_firmware.html", "web_firmware_dev.html")]


def crop_page(png):
    """Cuts the empty page background off the bottom (the window is taller than
    the page), keeping a margin like the existing screenshots."""
    img = Image.open(png).convert("RGB")
    w, h = img.size
    px = img.load()
    last = 0
    for y in range(h - 1, -1, -1):
        if any(px[x, y] != PAGE_BG for x in range(0, w, 2)):
            last = y
            break
    return img.crop((0, 0, w, min(h, last + 48)))


def shoot_page(browser, html, png):
    with tempfile.TemporaryDirectory() as profile:
        subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        "--force-device-scale-factor=2", "--window-size=500,3200",
                        "--user-data-dir=" + profile, "--screenshot=" + str(png),
                        html.as_uri()], check=True, capture_output=True, timeout=120)
    crop_page(png).save(png, optimize=True)
    print("wrote", png.relative_to(REPO), Image.open(png).size)


def shoot_device(host, password):
    url = "http://%s/screenshot.bmp?screen=%d" % (host, UPDATE_PROMPT_SCREEN)
    mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    mgr.add_password(None, "http://%s/" % host, "admin", password or "")
    opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(mgr))
    bmp = opener.open(url, timeout=30).read()
    png = OUT / "update_prompt.png"
    Image.open(io.BytesIO(bmp)).convert("RGB").save(png, optimize=True)
    print("wrote", png.relative_to(REPO), Image.open(png).size)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--device", help="board to take the display screenshot from")
    ap.add_argument("--password", default=os.environ.get("CYD_WEB_PASSWORD", ""))
    ap.add_argument("--no-web", action="store_true", help="skip the config page shots")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if not args.no_web:
        browser = find_browser()
        with tempfile.TemporaryDirectory() as tmp:
            for html in render_pages(Path(tmp)):
                shoot_page(browser, html, OUT / (html.stem + ".png"))
    if args.device:
        shoot_device(args.device, args.password)


if __name__ == "__main__":
    main()
