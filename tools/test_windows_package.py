#!/usr/bin/env python3
"""Run the staged Windows application without a developer toolchain on PATH."""

import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/package-validation"))
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this test requires Windows")
    prefix, raw, output = args.prefix.resolve(), args.raw.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    system_root = Path(os.environ["SystemRoot"])
    environment["PATH"] = os.pathsep.join((str(system_root / "System32"), str(system_root)))
    for key in ("QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "GTK_PATH", "GIO_EXTRA_MODULES"):
        environment.pop(key, None)
    environment["QT_QUICK_BACKEND"] = "software"
    environment["QT_FORCE_STDERR_LOGGING"] = "1"
    environment["LANGUAGE"] = "th_TH"
    for executable in ("darktable", "darktable-touch"):
        result = subprocess.run([str(prefix / "bin" / (executable + ".exe")), "--version"],
                                capture_output=True, env=environment, timeout=30)
        (output / (executable + "-version.log")).write_bytes(result.stdout + result.stderr)
        assert result.returncode in (0, 1) and b"darktable" in result.stdout.lower(), result
    for command_line in (False, True):
        name = "command-line" if command_line else "import"
        command = [str(prefix / "bin/darktable-touch.exe"), "-platform", "offscreen",
                   "--self-test", str(raw), "--screenshot", str(output / (name + ".png")),
                   "--coverage-report", str(output / (name + "-coverage.json"))]
        if command_line:
            command.append("--self-test-command-line")
        with (output / (name + ".log")).open("wb") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=environment,
                           check=True, timeout=120)
        report = json.loads((output / (name + "-coverage.json")).read_text(encoding="utf-8"))
        priorities = sorted((entry["priority"], entry["operation"]) for entry in report if entry["priority"])
        assert [operation for _, operation in priorities] == ["exposure", "agx", "lens", "denoiseprofile", "flip", "crop"]
        assert all(entry["touch_status"] in ("partial", "not_started") for entry in report)
    print("passed: standalone Classic/Touch startup, Unicode RAW workflows, PNG export, and module coverage")


if __name__ == "__main__":
    main()
