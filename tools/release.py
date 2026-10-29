"""Build release files for every board into dist/.

    python tools/release.py [env ...]

For each PlatformIO env (all of them by default) it builds the firmware and
writes:
  lanetag-<version>-<env>.bin          for the reader's update page (OTA)
  lanetag-<version>-<env>-factory.bin  bootloader, partitions and app in one
                                       image, flashed at 0x0 over USB
The version is LANETAG_VERSION when set (the release workflow sets it to the
tag), else `git describe`. Needs PlatformIO and esptool (pip install esptool).
"""
import configparser
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
# esptool's --chip for each env's board.
CHIPS = {
    "esp32dev": "esp32", "cyd": "esp32", "esp32-s3": "esp32s3", "esp32-c3": "esp32c3",
    "esp32-c6": "esp32c6", "esp32-c5": "esp32c5", "esp32-c6-lcd": "esp32c6", "esp32-c5-lcd": "esp32c5",
}


def pio(*args, **kw):
    return subprocess.run([sys.executable, "-m", "platformio", *args], cwd=ROOT, check=True, **kw)


def all_envs():
    ini = configparser.ConfigParser()
    ini.read(os.path.join(ROOT, "platformio.ini"))
    return [s[4:] for s in ini.sections() if s.startswith("env:")]


def version():
    v = os.environ.get("LANETAG_VERSION")
    if v:
        return v
    return subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, text=True).strip()


def main():
    envs = sys.argv[1:] or all_envs()
    unknown = [e for e in envs if e not in CHIPS]
    if unknown:
        sys.exit("no chip known for env(s) %s: add them to CHIPS" % ", ".join(unknown))
    ver = version()
    os.environ["LANETAG_VERSION"] = ver
    os.makedirs(DIST, exist_ok=True)
    for env in envs:
        pio("run", "-e", env)
        meta = json.loads(pio("project", "metadata", "-e", env, "--json-output",
                              capture_output=True, text=True).stdout)[env]["extra"]
        app = os.path.join(ROOT, ".pio", "build", env, "firmware.bin")
        name = os.path.join(DIST, "lanetag-%s-%s" % (ver, env))
        shutil.copyfile(app, name + ".bin")
        parts = []
        for image in meta["flash_images"]:
            parts += [image["offset"], image["path"]]
        # pioarduino leaves it out; min_spiffs.csv puts app0 there.
        parts += [meta.get("application_offset", "0x10000"), app]
        subprocess.run([sys.executable, "-m", "esptool", "--chip", CHIPS[env], "merge_bin",
                        "-o", name + "-factory.bin", *parts], check=True)
        print("%s: %s.bin, %s-factory.bin" % (env, os.path.basename(name), os.path.basename(name)))


if __name__ == "__main__":
    main()
