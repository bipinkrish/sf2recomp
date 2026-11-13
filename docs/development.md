# Development and verification

[Back to the README](../README.md#development).

Run development commands from the repository root.

## Local launcher and GPU diagnostics

The developer launcher `tools/run_linux.sh` selects hardware Vulkan when available,
uses the repository INI, and keeps run data under `build/run`. To select this
machine's Radeon explicitly:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json tools/run_linux.sh
```

`tools/run_linux.sh --software` selects lavapipe. On Linux RADV, the default
driver-managed heap shares coherent driver-allocated memory directly with the
CPU, preserving shader global-memory reads and writes without importing anonymous
guest pages. It selects the 2,560 MiB backing on drivers that cannot hold the
3,328 MiB allocation. Allocation or mapping failure stops boot.
`SF2RECOMP_VK_GUEST_HEAP=staged` uses anonymous guest memory and explicit uploads,
with a check for shaders that access the guest heap directly.
`SF2RECOMP_VK_GUEST_HEAP=managed` selects the 2,560 MiB backing explicitly on Linux.
`SF2RECOMP_VK_MANAGED_HEAP=0` restores the legacy path for developer
diagnostics; that path previously triggered AMD kernel faults on this machine.
Do not carry `SF2RECOMP_VK_NO_GMEM=1` or a software ICD override into a hardware test.

The opt-in memory check uses the production heap allocator without game files:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json python3 tools/test_gpu_heap.py
```

It requires Clang, Vulkan headers/loader and `glslc`, allocates the full 3.25 GiB
heap, checks CPU-to-compute-shader-to-CPU visibility at the beginning/middle/end,
and repeats with a fresh allocation. The regular unit tests require no GPU.

## Build and test

After an initial conversion has generated the game code:

```sh
make -j8
python3 -m unittest discover -s tests -v
```

`make gen` regenerates main/SDK from the default extraction after IL2CPP map setup.
See [generator instructions](../tools/recompr/README.md). `--check-game DIR` verifies
an executable's original NSO identities without opening a graphical window.

Tests cover corruption-free Vulkan page materialization, exact build identity,
configuration and environment validation, runtime key/gamepad remapping, INI edits
without rebuilding, portable paths, save journaling, extraction rollback and
conversion output contents. Mouse menu tests cover click edges, coordinate mapping,
focus/capture resets and Unity's UI input ABI. Native Linux checks cover hover,
Settings clicks and slider dragging, Controls and Sparring navigation, resizing
with black bars, and switching from mouse to keyboard. Menu captures do not
establish combat compatibility.

[Working findings](findings.md) retain historical debugging context; removed
experimental flags described in old entries are not current features.

## Combat replay investigation

The opt-in [combat replay harness](combat-replay.md) records logical fighter inputs
and compares combat-step traces across FPS and graphics settings. It is a
diagnostic experiment and is not a complete state serializer.
The [LAN Sparring implementation](lan-sparring.md) reuses its controlled setup
and observer to synchronize separate local game instances.
The harness guide describes fixed-settings divergence and the
[controlled initialization/RNG experiment](combat-replay.md#controlled-initialization-and-randomness).
Use `--seed` to opt into fixed-step native round/stance setup and scoped random streams.
Its PvP framing section explains how combat geometry keeps its original reference
aspect while the viewport honors the selected display aspect.

## Conversion output and packaging

The converter's output includes:

```text
Shadow Fight 2/
  sf2                               native executable, generated locally
  sf2recomp.ini                     runtime settings, controls and save path
  control.nacp / icon.png            official metadata/icon, extracted locally
  game-info.json                    official title/version provenance
  org.sf2recomp.ShadowFight2.desktop Linux launcher
  extracted/exefs/                  matching main, SDK and NPDM
  extracted/romfs/                  original game assets, copied locally
  lib/                             bundled fonts, FFmpeg and shared SDL when used
  licenses/                        notices and required dependency sources
  build.json                       source build IDs
  RUN.txt
```

Run `./sf2` from the output folder or any other working directory. This is a
Linux folder package, not an AppImage or Windows package.
`--package-only --game DIR` packages an existing local executable after checking
its main/SDK identities. Keys and tickets are excluded from the output.

The shared-font service supplies Noto Sans and Noto Sans CJK substitutes with
their OFL licenses. Keep `lib/fonts/` beside the executable. No console firmware
fonts are required; Nintendo's private icon font uses the standard fallback face.

## Dependency setup

`make` and conversion run `tools/setup_runtime.sh --ensure` to prepare missing
SDL/SMAA/fonts. Existing local or system SDL 3.4+ is reused. `--check` verifies
dependencies without downloads; `--build-sdl` explicitly rebuilds the pinned SDL
source. Setup verifies source checksums. See the
[native dependency guide](../tools/runtime-dependencies.md) for package names,
pinned sources and offline setup.

## Validation notes

Linux x86-64 renders the animated menu and reaches the English Story/Sensei
introduction. These screens were inspected after removing unrelated Zelda hooks.
The initial Story-dialogue exception now resumes through its guest LSDA landing
pad, and the intro cinematic is decoded and displayed by the host movie path.
Cleanup-only unwinding and rethrow are unsupported. Full combat stability, later
cinematics, and Windows remain unverified.

Rendering has been checked with X11/Xwayland on Mesa lavapipe software Vulkan,
an AMD Radeon 680M (RADV, Mesa 25.2.8), and an NVIDIA RTX 4070 Laptop GPU
(595.99.02). The Radeon uses a driver-owned, coherent guest heap with host-pointer
imports disabled. Selected menu/Story/tutorial tests and 3,000-frame runs reaching
a Kenji sparring fight passed strict Vulkan validation on both hardware GPUs.
Full-game stability and other GPUs remain unverified. The CPU game code executes
natively.

The runtime preserves original scene initialization and Unity internal calls.
Generated main and SDK build IDs must match the loaded files exactly before boot.
Only the tested update is accepted. Optional scaling, FSR/SMAA, filters, caches and
profiling remain; Zelda randomizer/cosmetic/launcher feature chains are removed.

The combined NSP update is applied: extraction takes the update ExeFS and merges
its BKTR RomFS over the base. The official update control metadata reports **1.1.0**
(base: 1.0.0). `v131072` is the content revision, not a display-version calculation.
Earlier notes calling it v2.0.0 were incorrect. The native window now shows
`Shadow Fight 2 (sf2recomp) - 1.1.0`, with version/icon read from the user's NACP
and original control icon. No game icon is shipped in the public source tree.

A guarded CPU buffer-copy change improved one matched software-Vulkan Story test
from 49.38 to 55.87 FPS (+13.1%). This is not a combat or native-GPU performance
claim. Working findings retain the historical measurements;
normal campaign Tournament combat has since been checked at 16:10, 21:9 and
32:9 (see the widescreen findings). Full combat stability and campaign completion
remain unverified.
