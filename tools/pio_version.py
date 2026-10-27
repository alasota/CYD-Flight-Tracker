# PlatformIO extra script (pre:) — gives the firmware a version number.
#
# Version = $FW_VERSION if set (CI can force one), else
# `git describe --tags --always --dirty`:
#   on tag v1.2.0                 -> v1.2.0
#   3 commits after it            -> v1.2.0-3-g1a2b3c4
#   no tag yet / local changes    -> 1a2b3c4 / 1a2b3c4-dirty
# else "0.0.0-unknown" (no git at all).
#
# - Passed as the FW_VERSION macro to src/admin/domain/firmware/version.cpp
#   only (build middleware), so a new version recompiles one file, not the
#   whole project.
# - After the build, copies firmware.bin to cyd-sky-tracker-<version>.bin in
#   the build directory and writes the version to version.txt there (the
#   GitHub Actions workflow reads it to name its files).

import os
import re
import shutil
import subprocess

Import("env")  # noqa: F821 — provided by PlatformIO/SCons

PROJECT_NAME = "cyd-sky-tracker"


def detect_version():
    forced = os.environ.get("FW_VERSION", "").strip()
    if forced:
        return forced
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip() or "0.0.0-unknown"
    except Exception:
        return "0.0.0-unknown"


# Safe for a C string, an HTML page and a file name.
VERSION = re.sub(r"[^0-9A-Za-z._+-]", "-", detect_version())
print("Firmware version: " + VERSION)


def add_version_define(env, node):
    defines = list(env.get("CPPDEFINES", []))  # a deque in newer SCons
    defines.append(("FW_VERSION", env.StringifyMacro(VERSION)))
    return env.Object(node, CPPDEFINES=defines)


env.AddBuildMiddleware(add_version_define, "*version.cpp")  # noqa: F821


def write_versioned_copy(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    shutil.copyfile(target[0].get_abspath(),
                    os.path.join(build_dir, "%s-%s.bin" % (PROJECT_NAME, VERSION)))
    with open(os.path.join(build_dir, "version.txt"), "w") as f:
        f.write(VERSION + "\n")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_versioned_copy)  # noqa: F821
