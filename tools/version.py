# PlatformIO pre-build script: the firmware's version and board, for the
# web page and /api/status.
#
# The version is `git describe` ("v0.2.0" on a release tag, "v0.2.0-3-gb86244a"
# after it, "-dirty" with uncommitted changes), or LANETAG_VERSION when set
# (the release workflow sets it to the tag). It goes into a generated
# lanetag_version.h (see src/version.h), rewritten only when it changes, so
# a new commit rebuilds only the files that include it.
import os
import subprocess

Import("env")


def git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        return ""


version = os.environ.get("LANETAG_VERSION") or git_version() or "unknown"
print("LaneTag %s (%s)" % (version, env["PIOENV"]))

folder = os.path.join(env.subst("$BUILD_DIR"), "generated")
os.makedirs(folder, exist_ok=True)
header = os.path.join(folder, "lanetag_version.h")
text = ("#pragma once\n#define FIRMWARE_VERSION \"%s\"\n#define FIRMWARE_BOARD \"%s\"\n"
        % (version.replace('"', ""), env["PIOENV"]))
try:
    with open(header) as f:
        same = f.read() == text
except OSError:
    same = False
if not same:
    with open(header, "w") as f:
        f.write(text)
env.Append(CPPPATH=[folder])
