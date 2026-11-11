# Experimental Windows x64 port

Windows host support, a native build profile, cutscene pipes, DLL packaging and
local smoke tests are implemented. **This port has not been compiled or run
on Windows in the development session that added it.** A successful Linux build
does not establish Windows compatibility. Integrated and dedicated Vulkan GPUs
are both intended targets; neither has been verified on Windows for SF2.

Extraction, update merging, IL2CPP analysis, C generation, compilation and
packaging now have native Windows entry points. The intended complete workflow
needs no Linux installation, WSL, Linux-prepared symbols or generated C. Native
Windows validation is still outstanding.

## Local inputs

Supply either an already extracted supported update, including `exefs`, `romfs`,
`control.nacp` and `icon.png`, or the supported combined base + update v131072 NSP
with its tickets/XML sidecars and your local keys. Existing extracted files need
no keys. The converter generates symbols and portable C on Windows as needed.

Windows game objects live in `build/windows`, native extraction tools in
`tools/bin/windows`, and their objects in `build/tools/extraction/windows`.
The .NET merger is built in `build/tools/bktr/windows`. Linux build outputs are
separate. The executable checks embedded main/SDK IDs against the original NSOs
before boot or packaging. Keep game inputs and generated code private.

## Windows build environment

Initial target: Windows 10 1903+ x64 or Windows 11, an x64 CPU with CMPXCHG16B,
and a Vulkan 1.3 graphics driver. The manifest selects UTF-8 for Windows/UCRT path
APIs. Windows on ARM and MSVC are not supported build profiles.

Install [MSYS2](https://www.msys2.org/) and open its **UCRT64** shell. Clang is
required for generated `musttail` calls; this profile uses Clang with the UCRT64
MinGW libraries and winpthreads. Keep the source checkout and compiler/dependency
prefix in paths without spaces for the Make/shell tooling. Converted game
folders can have spaces and Unicode names; runtime path buffers are bounded at
4096 UTF-8 bytes, and some diagnostics still use shorter Windows buffers.

Update MSYS2 using its documented update process, then install:

```sh
pacman -S --needed make curl tar \
  mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-python \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-sdl3 \
  mingw-w64-ucrt-x86_64-vulkan-headers \
  mingw-w64-ucrt-x86_64-vulkan-loader \
  mingw-w64-ucrt-x86_64-vulkan-memory-allocator \
  mingw-w64-ucrt-x86_64-shaderc
```

Install native **CPython 3.12 x64** and the **.NET 8+ SDK** on Windows. A .NET
runtime alone cannot build the merger or dumper. Put `dotnet.exe` on `PATH`, or
set `DOTNET='C:/Program Files/dotnet/dotnet.exe'` to its actual location.
Il2CppDumper keeps its pinned net6.0 target and uses runtime roll-forward on .NET
8+. The BKTR merger targets net8.0.

Create the Python environment from the UCRT64 shell in the checkout:

```sh
py -3.12 -m venv .venv
export PYTHONUTF8=1
export PYTHON="$(cygpath -m "$PWD/.venv/Scripts/python.exe")"
"$PYTHON" -m pip install -r tools/requirements.txt
```

Using the explicit interpreter keeps conversion on native CPython with available
Windows wheels, even if MSYS2's newer `python3` is also installed. Initial setup
downloads pinned native-tool sources and restores .NET dependencies from NuGet.

SDL 3.4+ is required. The dependency probe checks native Windows x64 compilation,
128-bit atomics, mandatory tail calls, SDL, Vulkan headers/loader, shaderc and VMA.
It does not open a window or test the GPU. SMAA and Noto fallback fonts use the
existing pinned, checksum-verified downloads.

```sh
bash tools/setup_windows.sh --ensure
make TARGET=windows -j4 runtime-objs
make TARGET=windows windows-smoke
```

The smoke executable needs no original game files. It checks reservation
collisions, lazy commitment on another thread, protections/reset, write-watch
where available, guarded reads, 64-bit positional I/O into guest memory, clocks,
Unicode paths, process arguments, and child exit status. Its large-offset file
test uses an NTFS sparse temporary file.

## Convert entirely on Windows

From the UCRT64 shell in the checkout:

```sh
export SDL_DIR="$(cygpath -m "$MINGW_PREFIX")"
"$PYTHON" tools/convert.py --target windows \
  --nsp 'D:/Game Dumps/Shadow Fight 2.nsp' \
  --keys 'D:/Private/prod.keys' \
  --output 'C:/Games/Shadow Fight 2'
```

For already extracted files, use `--game 'D:/Game Dumps/extracted/update'` in place
of `--nsp` and `--keys`. Both routes run local symbol analysis and C generation;
no symbols need to be copied from another computer. `--symbols FILE` optionally
reuses an existing map. `--skip-generation` optionally compiles existing generated
C when rebuilding, rather than generating it again.

The converter builds checksum-pinned hactool/NSTool sources with CMake/Ninja when
needed, merges the update with the .NET BKTR helper, and builds the pinned official
Il2CppDumper source. Analysis stages fresh outputs, validates `script.json` even
when the dumper returns zero, and preserves existing symbols if analysis fails.
Its cache checks the original NSO, metadata, config and symbol-map hashes.

Next it compiles a native `sf2.exe`, builds the checksum-pinned LGPL-only FFmpeg
recipe, validates the game identity, and packages the required DLLs, game assets,
fonts, licenses, FFmpeg's matching source and runtime INI. Initial compilation can
take time. `nasm` is optional; without it, FFmpeg's x86 assembly is disabled.

The preparation stages are also available independently:

```sh
"$PYTHON" tools/build_extraction_tools.py --target windows
"$PYTHON" tools/extract.py 'D:/Game Dumps/Shadow Fight 2.nsp' 'D:/Private/prod.keys'
"$PYTHON" tools/analyze_il2cpp.py game/extracted/update game/il2cpp
```

For an existing matching Windows build, use `--package-only` with `--game`.
`FFMPEG_DIR` can select another prefix built by this project's Windows FFmpeg
recipe; its default is `deps/ffmpeg-windows`. The Linux FFmpeg prefix is separate.
Do not substitute MSYS2's full FFmpeg package for this decoder recipe.

Launch `sf2.exe` from the output folder. Python, MSYS2, Make and compilers are
build-time tools. The packaged game should run without those installed. The
Vulkan loader/driver remains supplied by the installed graphics driver.

DLL dependencies are resolved recursively and case-insensitively using
`objdump -p`. Application DLLs go beside `sf2.exe`; decoder DLLs go beside
`lib/ffmpeg/bin/ffmpeg.exe`. Missing DLLs and MSYS/Cygwin runtime dependencies
are rejected before updating an existing package. Windows dependency licenses
are copied from the selected UCRT64 prefix. Rebuilding preserves the existing
INI and saves. `sf2.exe` launches without a console window. Explorer shows the
project icon and SF2Recomp file version. The game window uses the official icon
from the extracted game. The latest game log is `logs/shadow-fight-2.log` beside
`sf2.exe`. Recent sessions also have timestamped filenames in that folder.

## Graphics and cutscenes

The existing Vulkan renderer and its GPU selection remain shared with Linux.
Linux DMA-BUF allocation stays Linux-only. Windows uses the renderer's existing
host-pointer/chunk import paths, with all imported Windows 4 KiB pages touched.
The host reserves the fixed 12.5 GiB guest *virtual address* window and commits
pages as required; this does not allocate 12.5 GiB of resident RAM on startup.
Reservation failures never replace somebody else's mapping. Windows write-watch
is optional; unavailable tracking returns the conservative fallback indication.

No new graphics-memory fallback is claimed by this port. Correct shader memory,
coherence, limits and performance must be tested on actual Windows AMD/NVIDIA
dedicated GPUs as well as integrated GPUs. A successful Linux GPU run does not
prove the corresponding Windows driver works.

Windows movie playback uses SDL's native process API: video on stdout and PCM
audio on stderr, both drained concurrently. FFmpeg is launched in quiet mode so
diagnostic text cannot contaminate the audio stream; exit status is logged by
the host. The existing first-frame/audio startup synchronization is retained.
The same branch can be exercised on Linux using `SF2_MOVIE_SDL_PROCESS`, and its
integration test compares decoded PCM with an independent FFmpeg invocation and
checks cancellation while pipes are unread.

## Verification and remaining Windows validation

Run the local `runtime-objs` and `windows-smoke` Make targets above, then build
the pinned decoder with `python3 tools/setup_ffmpeg.py --target windows --ensure`.
These checks need no game files. They do not compile proprietary generated game
code or establish gameplay/GPU correctness.

Before describing this as a supported Windows release:

- Pass the local native compile/smoke checks and build/link the complete generated game.
- Convert/package from a clean checkout with the documented local inputs.
- Launch the package from Explorer and another working directory, with the
  developer tools removed from `PATH`, including a Unicode folder name.
- Exercise Story and Sparring, keyboard/controller input, audio/cutscenes,
  resizing/fullscreen, quit/relaunch, and save persistence.
- Validate representative AMD/NVIDIA dedicated and integrated GPU configurations
  with correct shader memory, inspect combat output, and check Vulkan validation.

The Windows host files were adapted from
[LAS Recomp](https://github.com/Peiikai/LASRecomp/tree/main/runtime/platform), under
the MIT notice in `licenses/LASRecomp.txt`. Imported upstream Git blob IDs:

| File | Upstream blob |
|---|---|
| `windows.c` | `7d0edde61aec9c63642c08482979a32a6bc8bdb2` |
| `win_compat.h` | `3b44e876d6ad69e7ae1829ce44737ca373205c3c` |
| `windows_threads.h` | `a2987010134eccbc35b5f1ffb436c338c1454215` |

Local adaptations include SF2 messages/CPU requirements, safe probes, bounded
command quoting, canonical paths, thread diagnostics, stricter memory errors,
and removal of unrelated child-process mitigation overrides.
