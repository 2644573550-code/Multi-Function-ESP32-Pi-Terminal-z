#!/usr/bin/env python3
"""Portable local-fixture generator and CTest runner. Python stdlib only."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + (b"\0" if len(data) % 2 else b"")


def generate_video(path):
    """8s, 24fps, 320x180 uncompressed AVI; no ffmpeg/codecs/download required."""
    width, height, fps, frames = 320, 180, 24, 192
    stride = (width * 3 + 3) & ~3
    frame_size = stride * height
    avih = struct.pack("<14I", 1000000 // fps, frame_size * fps, 0, 0x10,
                       frames, 0, 1, frame_size, width, height, 0, 0, 0, 0)
    strh = struct.pack("<4s4sIHH8I4h", b"vids", b"DIB ", 0, 0, 0, 0,
                       1, fps, 0, frames, frame_size, 0xFFFFFFFF, 0, 0, 0, width, height)
    strf = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, frame_size, 0, 0, 0, 0)
    hdrl = chunk(b"LIST", b"hdrl" + chunk(b"avih", avih)
                 + chunk(b"LIST", b"strl" + chunk(b"strh", strh) + chunk(b"strf", strf)))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Stream the file instead of holding all raw frames in memory.
    with path.open("wb") as output:
        output.write(b"RIFF\0\0\0\0AVI " + hdrl)
        movi_start = output.tell()
        output.write(b"LIST\0\0\0\0movi")
        index = bytearray()
        for frame in range(frames):
            color = bytes((40 + frame % 100, 140, 200))
            pixels = (color * width + bytes(stride - width * 3)) * height
            offset = output.tell() - movi_start - 8
            output.write(chunk(b"00db", pixels))
            index.extend(struct.pack("<4sIII", b"00db", 0x10, offset, len(pixels)))
        movi_end = output.tell()
        output.write(chunk(b"idx1", index))
        end = output.tell()
        output.seek(movi_start + 4)
        output.write(struct.pack("<I", movi_end - movi_start - 8))
        output.seek(4)
        output.write(struct.pack("<I", end - 8))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate-only", type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build/media-browser"))
    parser.add_argument("--configure", action="store_true", help="Configure and build the isolated VLC/browser tests")
    parser.add_argument("--backend-only", action="store_true", help="Explicitly omit real WebEngine tests (recorded in report)")
    parser.add_argument("--cmake-arg", action="append", default=[])
    parser.add_argument("--label", choices=["functional", "interface", "performance"])
    parser.add_argument("--max-startup-ms", type=float, default=5000)
    parser.add_argument("--min-fps", type=float, default=10)
    parser.add_argument("--max-heartbeat-ms", type=float, default=200)
    parser.add_argument("--max-cpu-percent", type=float, default=200)
    args = parser.parse_args()
    if args.generate_only:
        print(generate_video(args.generate_only))
        return 0
    root = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    artifacts = build / "artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TERMINAL_TEST_ARTIFACTS"] = str(artifacts)
    for name in ["max_startup_ms", "min_fps", "max_heartbeat_ms", "max_cpu_percent"]:
        env["TERMINAL_" + name.upper()] = str(getattr(args, name))
    cmake, ctest = shutil.which("cmake"), shutil.which("ctest")
    if not cmake or not ctest:
        parser.error("cmake and ctest are required on PATH")
    if args.configure:
        configure = [cmake, "-S", str(root / "third_party/vlc"), "-B", str(build),
                     "-DBUILD_TESTING=ON", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                     "-DTERMINAL_BROWSER_TESTS=" + ("OFF" if args.backend_only else "ON")]
        subprocess.run(configure + args.cmake_arg, check=True, env=env)
        subprocess.run([cmake, "--build", str(build), "--config", "RelWithDebInfo", "--parallel", "2"], check=True, env=env)
    discovery = subprocess.run([ctest, "--test-dir", str(build), "--show-only=json-v1"],
                               env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    suite = json.loads(discovery.stdout)
    available = {test["name"] for test in suite["tests"]}
    required = {"media_vlc_functional", "media_vlc_performance", "media_vlc_interface",
                "media_browser_joystick_interface", "media_video_player_interface"}
    if not args.backend_only:
        required.add("media_browser_context_functional")
    missing = required - available
    if missing:
        parser.error("Incomplete test build; missing " + ", ".join(sorted(missing)) + ". Reconfigure/rebuild first.")
    command = [ctest, "--test-dir", str(build), "-C", "RelWithDebInfo", "--output-on-failure", "-V",
               "--output-junit", str(artifacts / "junit.xml"), "-R", "^media_"]
    if args.label:
        command += ["-L", args.label]
    start = time.monotonic()
    run = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(run.stdout, end="")
    (artifacts / "test.log").write_text(run.stdout, encoding="utf-8")
    metrics = [json.loads(match) for match in re.findall(r"MEDIA_METRICS\s+(\{[^\n]*\})", run.stdout)]
    # A missing/misconfigured suite must not look like a passing test run.
    no_tests = "No tests were found" in run.stdout or not re.search(r"\d+% tests passed", run.stdout)
    report = {"exit_code": run.returncode or int(no_tests), "elapsed_seconds": time.monotonic() - start,
              "real_browser_requested": not args.backend_only, "label": args.label,
              "available_tests": sorted(available), "metrics": metrics,
              "binding_loop_warning_count": run.stdout.count("Binding loop detected"),
              "window_screenshot_unavailable": "Window screenshot unavailable" in run.stdout,
              "hardware_validation": "not performed by this script"}
    (artifacts / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
