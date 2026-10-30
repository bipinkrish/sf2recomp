#!/usr/bin/env python3
"""Prepare the supported combined SF2 base/update NSP using user-supplied keys."""
import argparse
import importlib.util
from dataclasses import dataclass
from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from game_metadata import write_game_metadata

TOOLS = Path(__file__).resolve().parent
TITLE_IDS = {"Application": 0x0100AB000ABFE000, "Patch": 0x0100AB000ABFE800}
UPDATE_BUILD_ID = "E15FC665EC42684AA6EDD1AE03624D96"


@dataclass(frozen=True)
class Entry:
    offset: int
    size: int


def pfs0_entries(path):
    """Validate names and ranges before handing this container to native tools."""
    size = path.stat().st_size
    with path.open("rb") as source:
        header = source.read(16)
        if len(header) != 16 or header[:4] != b"PFS0":
            raise ValueError("expected an uncompressed PFS0 NSP")
        _, count, strings_size, _ = struct.unpack("<4sIII", header)
        data_start = 16 + count * 24 + strings_size
        if count > 10000 or strings_size > 16 * 1024 * 1024 or data_start > size:
            raise ValueError("invalid NSP file table")
        table, strings = source.read(count * 24), source.read(strings_size)
        entries = {}
        for index in range(count):
            offset, length, name_offset, _ = struct.unpack_from("<QQII", table, index * 24)
            if name_offset >= len(strings) or b"\0" not in strings[name_offset:]:
                raise ValueError("invalid NSP filename")
            name = strings[name_offset:strings.index(0, name_offset)].decode("utf-8")
            if not name or name in (".", "..") or any(c in name for c in "/\\:\r\n") or name in entries:
                raise ValueError("NSP contains an unsafe or duplicate filename")
            if data_start + offset + length > size:
                raise ValueError("NSP contains an out-of-range file")
            entries[name] = Entry(data_start + offset, length)
    return entries


def select_programs(path, entries):
    """This version supports one combined base + v131072 bundle with XML metadata."""
    selected = {}
    with path.open("rb") as source:
        for name, entry in entries.items():
            if not name.endswith(".cnmt.xml"):
                continue
            if entry.size > 1024 * 1024:
                raise ValueError("CNMT XML is too large")
            source.seek(entry.offset)
            meta = ET.fromstring(source.read(entry.size))
            kind = meta.findtext("Type")
            if kind not in TITLE_IDS:
                continue
            if int(meta.findtext("Id", "0"), 0) != TITLE_IDS[kind]:
                raise ValueError("bundle contains a different game; expected Shadow Fight 2")
            if kind in selected:
                raise ValueError("bundle contains multiple base or update versions")
            if kind == "Patch" and int(meta.findtext("Version", "-1")) != 131072:
                raise ValueError("unsupported update: this pipeline requires v131072")
            programs = [content.findtext("Id", "") for content in meta.findall("Content")
                        if content.findtext("Type") == "Program"]
            if len(programs) != 1 or not re.fullmatch(r"[0-9a-fA-F]{32}", programs[0]):
                raise ValueError("CNMT must identify exactly one program NCA")
            filename = programs[0].lower() + ".nca"
            if filename not in entries:
                raise ValueError("CNMT program NCA is missing from the bundle")
            tickets = [name for name in entries if name.lower().startswith(f"{TITLE_IDS[kind]:016x}")
                       and name.endswith(".tik")]
            if len(tickets) != 1:
                raise ValueError(f"expected exactly one {kind} ticket in the bundle")
            selected[kind] = filename, tickets[0]
    if set(selected) != set(TITLE_IDS):
        raise ValueError("supported input is a combined SF2 base + v131072 update NSP with .cnmt.xml "
                         "sidecars and tickets; separate packages or missing XML are not supported yet")
    return selected



def select_control(path, entries):
    """Prefer update control content; use base only when update declares none."""
    controls = {}
    with path.open("rb") as source:
        for name, entry in entries.items():
            if not name.endswith(".cnmt.xml"):
                continue
            if entry.size > 1024 * 1024:
                raise ValueError("CNMT XML is too large")
            source.seek(entry.offset)
            meta = ET.fromstring(source.read(entry.size))
            kind = meta.findtext("Type")
            if kind not in TITLE_IDS:
                continue
            if int(meta.findtext("Id", "0"), 0) != TITLE_IDS[kind]:
                raise ValueError("control metadata belongs to a different game")
            ids = [content.findtext("Id", "") for content in meta.findall("Content")
                   if content.findtext("Type") == "Control"]
            if not ids:
                continue
            if len(ids) != 1 or kind in controls or not re.fullmatch(r"[0-9a-fA-F]{32}", ids[0]):
                raise ValueError("CNMT must identify at most one control NCA per version")
            filename = ids[0].lower() + ".nca"
            if filename not in entries:
                raise ValueError("CNMT control NCA is missing from the bundle")
            controls[kind] = filename
    for kind in ("Patch", "Application"):
        if kind in controls:
            return kind, controls[kind]
    raise ValueError("bundle lacks official control metadata and icon content")


def publish(stage, game):
    """Replace both output directories, rolling back if a rename fails."""
    moved, installed = [], []
    try:
        for name in ("nsp", "extracted"):
            target = game / name
            if target.exists():
                target.rename(stage / ("old-" + name))
                moved.append(name)
            (stage / name).rename(target)
            installed.append(name)
    except OSError:
        for name in reversed(installed):
            shutil.rmtree(game / name)
        for name in reversed(moved):
            (stage / ("old-" + name)).rename(game / name)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nsp", type=Path)
    parser.add_argument("keys", type=Path, help="your local prod.keys; never uploaded or logged")
    parser.add_argument("--game-dir", type=Path, default=TOOLS.parent / "game")
    parser.add_argument("--validate-only", action="store_true", help="check the packaged layout without extraction")
    args = parser.parse_args(argv)
    try:
        nsp, keys, game = args.nsp.resolve(strict=True), args.keys.resolve(strict=True), args.game_dir.resolve()
        if not keys.is_file():
            raise ValueError("keys must be a readable file")
        entries = pfs0_entries(nsp)
        programs = select_programs(nsp, entries)
        control_kind, control_filename = select_control(nsp, entries)
        if args.validate_only:
            print("Supported SF2 base + v131072 bundle layout validated (decryption not checked).")
            return 0
        if importlib.util.find_spec("PIL") is None:
            raise ValueError("install icon conversion dependencies: python3 -m pip install -r tools/requirements.txt")
        gc, nt = TOOLS / "bin/hactool", TOOLS / "bin/nstool"
        if not all(os.access(tool, os.X_OK) for tool in (gc, nt)):
            raise ValueError("build extraction tools first: tools/build_extraction_tools.sh")
        dotnet = os.environ.get("DOTNET", "dotnet")
        if not shutil.which(dotnet):
            raise ValueError("install the .NET 8 SDK to build/run the BKTR merger")
        # Private staging keeps tickets/keys local, and failures preserve previous results.
        os.umask(0o077)
        game.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".extract-", dir=game) as temporary:
            stage = Path(temporary)
            (stage / "nsp").mkdir()
            run = lambda *command: subprocess.run([str(arg) for arg in command], check=True)
            run(gc, "-t", "pfs0", "--pfs0dir=" + str(stage / "nsp"), nsp)
            run(sys.executable, TOOLS / "extract_titlekeys.py", "--keys", keys, "--nsp-dir", stage / "nsp")
            base, update = stage / "extracted/base", stage / "extracted/update"
            base.mkdir(parents=True)
            update.mkdir()
            for kind, destination in (("Application", base), ("Patch", update)):
                filename, ticket = programs[kind]
                run(nt, "-k", keys, "--tik", stage / "nsp" / ticket,
                    "--part0", destination / "exefs", stage / "nsp" / filename)
            with (update / "exefs/main").open("rb") as source:
                main_header = source.read(0x60)
            if main_header[:4] != b"NSO0" or main_header[0x40:0x50].hex().upper() != UPDATE_BUILD_ID:
                raise ValueError("unsupported main NSO build; expected SF2 update v131072")
            for name in ("main", "subsdk0", "sdk", "rtld"):
                run(gc, "-t", "nso0", "--disablekeywarns", "--uncompressed=" + str(update / "exefs" / (name + ".uncompressed")),
                    update / "exefs" / name)
            base_nca, base_ticket = programs["Application"]
            for partition, name in (("--part1", "romfs"), ("--part2", "logo")):
                run(nt, "-k", keys, "--tik", stage / "nsp" / base_ticket, partition, base / name, stage / "nsp" / base_nca)
            merger = TOOLS / "bktr/bin/Release/net8.0/Sf2Bktr.dll"
            run(dotnet, "build", TOOLS / "bktr/Sf2Bktr.csproj", "-c", "Release", "--nologo")
            environment = dict(os.environ, SF2BKTR_DUMP_OUT=str(update / "romfs.img"))
            subprocess.run([dotnet, str(merger), str(stage / "nsp" / programs["Patch"][0]),
                            str(stage / "nsp" / base_nca), str(keys), str(stage / "nsp/title.keys")],
                           env=environment, check=True)
            run(nt, "-t", "romfs", "-x", update / "romfs", update / "romfs.img")
            if not (update / "romfs/Data/Managed/Metadata/global-metadata.dat").is_file():
                raise ValueError("merged RomFS lacks the required IL2CPP metadata")
            # The display version comes from the official NACP, not CNMT's ordinal.
            control = stage / "control"
            run(nt, "-k", keys, "--part0", control, stage / "nsp" / control_filename)
            info = write_game_metadata(control, update, control_kind)
            print(f"Official game metadata: {info['title']} {info['display_version']} ({control_kind})")
            publish(stage, game)
        print("Extraction complete:", game / "extracted/update")
        return 0
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        print("Extraction failed:", error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
