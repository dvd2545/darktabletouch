#!/usr/bin/env python3
"""Exercise a real RAW workflow through the headless engine under Unicode paths."""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def image_data(png):
    assert png.startswith(b"\x89PNG\r\n\x1a\n"), "preview is not a PNG"
    offset, compressed = 8, bytearray()
    while offset < len(png):
        length = struct.unpack(">I", png[offset:offset + 4])[0]
        kind = png[offset + 4:offset + 8]
        if kind == b"IDAT":
            compressed.extend(png[offset + 8:offset + 8 + length])
        offset += length + 12
    return zlib.decompress(compressed)


class Engine:
    def __init__(self, binary, build, work):
        self.serial = 0
        self.log = (work / "engine.log").open("a", encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(build / "bin") + os.pathsep + env["PATH"]
        env["LANGUAGE"] = "th_TH"
        args = [str(binary), "--core", "--disable-opencl",
                "--datadir", str(build / "share/darktable"),
                "--moduledir", str(build / "lib/darktable"),
                "--localedir", str(build / "share/locale"),
                "--configdir", str(work / "ผู้ใช้/ตั้งค่า"),
                "--cachedir", str(work / "ผู้ใช้/แคช"),
                "--conf", "write_sidecar_files=on import",
                "--conf", "host_memory_limit=2048",
                "--conf", "worker_threads=2", "-t", "2"]
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, encoding="utf-8", env=env)
        self.call("initialize", {})

    def call(self, method, params):
        self.serial += 1
        self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.serial,
                                             "method": method, "params": params}) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("engine stopped before responding; see engine.log")
        response = json.loads(line)
        if "error" in response:
            raise RuntimeError(response["error"])
        result = response["result"]
        if isinstance(result, dict) and result.get("isError"):
            raise RuntimeError(result)
        return result

    def tool(self, name, arguments):
        return self.call("tools/call", {"name": name, "arguments": arguments})

    @staticmethod
    def decode(result):
        return json.loads(next(item["text"] for item in result["content"] if item["type"] == "text"))

    def render(self, image_id, edit=False):
        args = {"input": {"imgid": image_id}, "width": 512, "height": 512}
        if edit:
            args["stack"] = [{"operation": "exposure", "params": {"exposure": 0.5}, "enabled": True}]
        result = self.tool("render", args)
        return base64.b64decode(next(item["data"] for item in result["content"] if item["type"] == "image"))

    def close(self):
        try:
            if self.process.poll() is None:
                self.call("shutdown", {})
        finally:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=10)
            self.log.close()


def run(binary, build, source, work):
    folder = work / "ผู้ใช้/ทดสอบ ภาพถ่าย #100%/กำลัง_กําลัง_🌅"
    folder.mkdir(parents=True)
    thai = folder / ("รูปที่ ๑" + source.suffix)
    english = work / ("reference" + source.suffix)
    shutil.copyfile(source, thai)
    shutil.copyfile(source, english)
    original_hash = hashlib.sha256(thai.read_bytes()).digest()
    engine = Engine(binary, build, work)
    try:
        imported = engine.decode(engine.tool("import_images", {"paths": [str(thai), str(english)]}))
        images = imported["images"]
        assert all(item["imgid"] > 0 for item in images), imported
        assert Path(images[0]["path"]).name == thai.name, images[0]
        image_id = images[0]["imgid"]
        repeated = engine.decode(engine.tool("import_images", {"paths": [str(thai)]}))
        assert repeated["images"][0]["imgid"] == image_id, "repeat import created a duplicate"
        preview = engine.render(image_id, edit=True)
        reference = engine.render(images[1]["imgid"], edit=True)
        assert image_data(preview) == image_data(reference), "filename affected rendered pixels"
        (folder / "พรีวิว.png").write_bytes(preview)
        sidecar = Path(str(thai) + ".xmp")
        assert sidecar.exists(), "sidecar missing"
        sidecar.read_text(encoding="utf-8")
        output = folder / "ส่งออก.png"
        engine.tool("export_images", {"input": {"imgid": image_id}, "out_path": str(output),
                                      "format": "png", "width": 512, "height": 512})
        assert output.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "export missing or invalid"
    finally:
        engine.close()
    engine = Engine(binary, build, work)
    try:
        assert image_data(engine.render(image_id)) == image_data(preview), "edits did not survive restart"
        assert hashlib.sha256(thai.read_bytes()).digest() == original_hash, "source RAW was modified"
    finally:
        engine.close()
    print("passed: Unicode RAW import, duplicate detection, Exposure, preview, XMP, export, and restart")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, default=Path("build"))
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--keep", type=Path, help="retain the test catalog and engine log in this new directory")
    args = parser.parse_args()
    build = args.build_root.resolve()
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [build / "bin/mcp" / ("darktable-mcp" + suffix),
                  build / "bin" / ("darktable-mcp" + suffix)]
    binary = next((path for path in candidates if path.exists()), None)
    if not binary:
        parser.error("build darktable-mcp before running the workflow test")
    if args.keep:
        work = args.keep.resolve()
        work.mkdir(parents=True, exist_ok=False)
        run(binary, build, args.raw.resolve(), work)
    else:
        with tempfile.TemporaryDirectory(prefix="darktabletouch-unicode-") as directory:
            run(binary, build, args.raw.resolve(), Path(directory))


if __name__ == "__main__":
    main()
