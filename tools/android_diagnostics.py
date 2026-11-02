#!/usr/bin/env python3
"""Collect SF2 logs across app restarts without copying saves or clearing data."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="ADB device serial")
    parser.add_argument("--adb", default=str(Path(os.environ.get("ANDROID_HOME", Path.home() / "Android/Sdk")) / "platform-tools/adb"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--watch-seconds", type=float, default=0)
    parser.add_argument("--launch", action="store_true", help="restart the game to measure startup")
    args = parser.parse_args()
    if args.watch_seconds < 0:
        parser.error("--watch-seconds must be nonnegative")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = args.output or Path("build/validation") / ("android-diagnostics-" + stamp)
    output.mkdir(parents=True, exist_ok=True)
    adb = [args.adb, "-s", args.serial]

    def read(*command):
        return subprocess.run(adb + list(command), stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout

    root = "/sdcard/Android/data/org.sf2recomp.app/files/"
    (output / "sf2recomp.ini").write_bytes(read("exec-out", "cat", root + "sf2recomp.ini"))
    if args.launch:
        read("shell", "am", "force-stop", "org.sf2recomp.app")
        read("shell", "am", "start", "-n", "org.sf2recomp.app/.GameActivity")
    deadline = time.monotonic() + args.watch_seconds
    while True:
        log = read("exec-out", "cat", root + "lasrecomp.log")
        header = log.split(b"\n", 1)[0]
        pid = re.search(rb"pid (\d+)", header)
        name = "game-pid-" + pid.group(1).decode() if pid else "game"
        (output / (name + ".log")).write_bytes(log)
        if time.monotonic() >= deadline:
            break
        time.sleep(min(2, max(0, deadline - time.monotonic())))
    (output / "startup-logcat.txt").write_bytes(read("logcat", "-d", "-v", "threadtime", "-s", "ShadowFight2:*"))
    print(output.resolve())


if __name__ == "__main__":
    main()
