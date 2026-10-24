#!/usr/bin/env python3
"""Exercise the native launcher with isolated child-process probes."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, default=Path("build"))
    args = parser.parse_args()
    suffix = ".exe" if os.name == "nt" else ""
    binaries = args.build_root.resolve() / "bin"
    with tempfile.TemporaryDirectory(prefix="darktabletouch-launcher-") as temporary:
        root = Path(temporary) / "ผู้ใช้ ทดสอบ 🚀 % #"
        root.mkdir()
        launcher = root / ("darktabletouch" + suffix)
        shutil.copy2(binaries / launcher.name, launcher)
        for child in ("darktable-touch", "darktable"):
            shutil.copy2(binaries / ("test-touch-launcher-child" + suffix), root / (child + suffix))
        config = root / "ตั้งค่า"
        path = r"C:\Users\ผู้ใช้\Pictures\ทดสอบ ภาพถ่าย\รูปที่ ๑🚀 % #.CR3"
        command = [str(launcher), "--configdir", str(config), path]
        subprocess.run(command, check=True, timeout=30)
        log = config / "children.jsonl"
        children = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        assert [child["ui"] for child in children] == ["touch", "classic"]
        assert [child["restoring"] for child in children] == [False, True]
        assert all(child["arguments"] == command[1:] for child in children)
        log.unlink()
        subprocess.run(command, check=True, timeout=30)
        assert json.loads(log.read_text(encoding="utf-8"))["ui"] == "classic"
        log.unlink()
        result = subprocess.run(command + ["--ui=touch", "--probe-fail"], timeout=30)
        assert result.returncode == 7
        assert json.loads(log.read_text(encoding="utf-8"))["ui"] == "touch"
        log.unlink()
        subprocess.run(command + ["--ui=touch", "--probe-stale"], check=True, timeout=30)
        assert json.loads(log.read_text(encoding="utf-8"))["ui"] == "touch"
    print("passed: Unicode process paths, UI handoff, saved selection, failed child, and stale request")


if __name__ == "__main__":
    main()
