#!/usr/bin/env python3
"""Build pinned native hactool/NSTool executables on Linux or Windows UCRT64."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from host_tools import ROOT, digest, host_target, native_executable, require_host, run, tool_directory, tool_path
import setup_extraction


def recipe(target):
    return {"target": target, "lock": digest(ROOT / "tools/extraction-dependencies.json"),
            "cmake": digest(ROOT / "tools/extraction/CMakeLists.txt"),
            "driver": digest(ROOT / "tools/build_extraction_tools.py"),
            "manifest": digest(ROOT / "runtime/platform/windows.manifest") if target == "windows" else None,
            "resource": digest(ROOT / "tools/extraction/windows.rc.in") if target == "windows" else None}


def installed(target):
    try:
        record = json.loads((tool_directory(target) / ".sf2-tools.json").read_text(encoding="utf-8"))
        names = {tool_path(name, target).name for name in ("hactool", "nstool")}
        return (record["recipe"] == recipe(target) and names == record["files"].keys()
                and all(native_executable(tool_directory(target) / name, target)
                        and digest(tool_directory(target) / name) == value for name, value in record["files"].items()))
    except (OSError, ValueError, KeyError):
        return False


def build(target=None, jobs=None, offline=False):
    target = target or host_target()
    require_host(target)
    jobs = jobs if jobs is not None else int(os.environ.get("JOBS", min(os.cpu_count() or 1, 8)))
    if jobs < 1:
        raise ValueError("JOBS/--jobs must be positive")
    for command in ("cmake", "ninja", "clang", "clang++"):
        if not shutil.which(command):
            raise ValueError(f"Missing input-tool build dependency: {command}; see docs/windows.md or tools/README.md")
    sources = ROOT / "tools/third_party"
    archives = Path(os.environ.get("SF2_SOURCE_ARCHIVES", ROOT / "build/dependencies/archives")).resolve()
    setup_extraction.prepare(sources, archives, offline=offline)
    build_dir = ROOT / "build/tools/extraction" / target
    run("cmake", "-S", ROOT / "tools/extraction", "-B", build_dir, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++")
    run("cmake", "--build", build_dir, "--parallel", jobs, "--target", "hactool", "nstool")
    outputs = [build_dir / "bin" / tool_path(name, target).name for name in ("hactool", "nstool")]
    if not all(native_executable(path, target) for path in outputs):
        raise ValueError("Input-tool build did not produce the expected native executable format")
    destination = tool_directory(target)
    destination.mkdir(parents=True, exist_ok=True)
    for source in outputs:
        staged = destination / (source.name + ".tmp")
        shutil.copy2(source, staged)
        staged.replace(destination / source.name)
    marker = destination / ".sf2-tools.json"
    pending = marker.with_suffix(".tmp")
    pending.write_text(json.dumps({"recipe": recipe(target), "files": {p.name: digest(p) for p in outputs}}, indent=2)
                       + "\n", encoding="utf-8")
    pending.replace(marker)
    print(f"Native {target} extraction tools ready: {destination}")


def ensure_tools(target=None):
    target = target or host_target()
    if not installed(target):
        build(target)
    return tool_path("hactool", target), tool_path("nstool", target)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("linux", "windows"), default=host_target())
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--offline", action="store_true", help="use the existing checksum-verified source archive cache")
    args = parser.parse_args(argv)
    try:
        build(args.target, args.jobs, args.offline)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Extraction-tool build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
