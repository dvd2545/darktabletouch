#!/usr/bin/env python3
"""measure the Qt Touch on copied fixtures without opening the user's catalog"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile
import threading
import time
from datetime import datetime, timezone


TARGETS = {
    "launch_library_ms": 1500,
    "thumbnail_scroll_frame_ms": 1000 / 60,
    "tap_first_picture_ms": 100,
    "fullscreen_render_ms": 1000,
    "prefetched_swipe_ms": 50,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, action="append", required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.rounds < 3:
        parser.error("at least three rounds are required")
    build = args.build.resolve()
    if "CMAKE_BUILD_TYPE:STRING=Release" not in (build / "CMakeCache.txt").read_text(encoding="utf-8"):
        parser.error("use a Release build for comparable timings")
    fixtures = [path.resolve(strict=True) for path in args.fixture]
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    executable = build / "bin" / ("test-touch-baseline.exe" if os.name == "nt" else "test-touch-baseline")
    env = dict(os.environ, DARKTABLETOUCH_PERF="1", QT_FORCE_STDERR_LOGGING="1", QSG_INFO="1")
    for name in ("DARKTABLETOUCH_DISABLE_PRELOAD", "DARKTABLETOUCH_DISABLE_PIPE_CACHE",
                 "DARKTABLETOUCH_TEST_AVAILABLE_MEMORY_MIB"):
        if name in env:
            parser.error(f"unset {name} before measuring")
    machine = {"platform": platform.platform(), "architecture": platform.machine()}
    if os.name == "nt":
        machine.update(json.loads(subprocess.check_output([
            "powershell", "-NoProfile", "-Command",
            "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); "
            "$c = Get-CimInstance Win32_ComputerSystem; "
            "@{cpu=(Get-CimInstance Win32_Processor).Name; "
            "memory_bytes=$c.TotalPhysicalMemory; model=$c.Model; "
            "power_scheme=(powercfg /getactivescheme | Out-String).Trim(); "
            "battery=@(Get-CimInstance Win32_Battery | Select-Object BatteryStatus,EstimatedChargeRemaining); "
            "gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)} "
            "| ConvertTo-Json -Depth 4"], text=True, encoding="utf-8")))
    report = {
        "schema_version": 1,
        "measured_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], text=True).strip()),
        "machine": machine, "build_type": "Release", "opencl": False,
        "window_logical_pixels": [1280, 720], "rounds_per_fixture": args.rounds,
        "protocol": {
            "launch": "parent process start to received first-swapped-Library marker with populated persisted collection; includes OS loader and observer scheduling, excludes launcher",
            "scroll": "120 successive presented frames, contentY traverses the entire 24-photo grid; includes thumbnail loading",
            "tap": "synthetic thumbnail mouse press/release to swapped frame with ready current QML Image",
            "render": "same tap to last swapped current image, including QML viewport refinement; default 1280-pixel preview quality; no XMP edits",
            "swipe": "navigateImage(1), the recognized-swipe command, to swapped ready image; verified prepared neighbor; excludes gesture recognition",
            "cache": "fresh catalog per fixture; import outside timings; fresh process each round, persistent thumbnail cache and warm OS file cache",
            "qml": "same compiled Main.qml and components as darktable-touch; real native window; no physical touch or scanout measurement",
        },
        "fixtures": [], "samples": [],
    }
    with executable.open("rb") as binary:
        report["benchmark_executable_sha256"] = hashlib.file_digest(binary, "sha256").hexdigest()
    with tempfile.TemporaryDirectory(prefix="touch-baseline-") as temporary:
        for fixture_index, fixture in enumerate(fixtures):
            work = Path(temporary) / str(fixture_index)
            photos = work / "photos"
            photos.mkdir(parents=True)
            with fixture.open("rb") as source:
                checksum = hashlib.file_digest(source, "sha256").hexdigest()
            report["fixtures"].append({
                "name": fixture.name, "bytes": fixture.stat().st_size,
                "sha256": checksum,
                "catalog_copies": 24, "sidecars_copied": False,
            })
            for index in range(24):
                shutil.copyfile(fixture, photos / f"photo-{index:02d}{fixture.suffix}")
            for round_index in range(-1, args.rounds):
                checkpoint = work / "round.json"
                command = [str(executable), "--work", str(work), "--build", str(build), "--output", str(checkpoint)]
                if round_index < 0:
                    command.append("--seed")
                else:
                    command.extend(["--capture", str(output.with_name(f"{output.stem}-fixture-{fixture_index}-round-{round_index}.png"))])
                log_path = output.with_name(f"{output.stem}-fixture-{fixture_index}-round-{round_index}.log")
                print(f"{fixture.name}: {'seed' if round_index < 0 else f'round {round_index + 1}'}", flush=True)
                library_visible = []
                with log_path.open("w", encoding="utf-8") as log:
                    started = time.perf_counter()
                    with subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          text=True, encoding="utf-8", errors="replace", bufsize=1) as process:
                        def read_log():
                            for line in process.stdout:
                                if line.strip() == "baseline-library-visible":
                                    library_visible.append((time.perf_counter() - started) * 1000)
                                log.write(line)
                        reader = threading.Thread(target=read_log)
                        reader.start()
                        try:
                            process.wait(timeout=300)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                            raise
                        finally:
                            reader.join()
                        if process.returncode:
                            raise subprocess.CalledProcessError(process.returncode, command)
                if round_index >= 0:
                    sample = json.loads(checkpoint.read_text(encoding="utf-8"))
                    if len(library_visible) != 1:
                        raise RuntimeError("missing or repeated Library presentation marker")
                    sample["launch_library_ms"] = library_visible[0]
                    if sample["platform_plugin"] in ("offscreen", "minimal") or sample["graphics_api"] in (0, 1, 7):
                        raise RuntimeError("baseline requires a native window with GPU rendering")
                    if [sample["window_width"], sample["window_height"]] != [1280, 720]:
                        raise RuntimeError("baseline requires a screen with 1280x720 logical pixels")
                    sample.update(fixture=fixture_index, round=round_index + 1)
                    report["samples"].append(sample)
    report["metrics"] = {}
    for metric, target in TARGETS.items():
        values = [value for sample in report["samples"]
                  for value in (sample[metric] if isinstance(sample[metric], list) else [sample[metric]])]
        median, worst = statistics.median(values), max(values)
        report["metrics"][metric] = {
            "median_ms": median, "worst_ms": worst, "sample_count": len(values),
            "target_ms": target, "median_beats_target": median < target,
            "worst_beats_target": worst < target,
            "comparison_median_limit_ms": min(target, median),
            "comparison_worst_limit_ms": min(target, worst),
        }
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report["metrics"], indent=2), flush=True)


if __name__ == "__main__":
    main()
