# Experimental macOS port

Native macOS arm64 (Apple Silicon) and x86-64 (Intel) build and conversion paths
are implemented. Native compilation and gameplay have not been verified in this
Linux development session. Local native build checks and hardware playtesting
are still required on both architectures.

The host layer is adapted from
[LASRecomp's macOS implementation](https://github.com/peiikai/LASRecomp/blob/main/runtime/platform/macos.c)
under its [MIT license](../licenses/LASRecomp.txt). It uses Mach guest-memory
reservation/reset, safe memory reads, native timing, pthreads and child processes.
Crash handling supports both architectures. Write tracking and cross-thread
backtrace capture are unavailable.

SF2 keeps its existing Vulkan renderer, using
[MoltenVK](https://github.com/KhronosGroup/MoltenVK) over Metal. Instance portability
enumeration and the advertised device portability extension are enabled. This
does not import LASRecomp's Metal renderer or establish SF2 GPU compatibility.
The renderer requires a Vulkan 1.3-capable MoltenVK installation; older 1.2-only
versions are insufficient.

## Prepare the Mac

Use a native Terminal session and dependencies matching the machine's
architecture. Apple Silicon builds should use arm64 Python, Homebrew and Clang
throughout. Intel builds are native x86-64 builds. Universal binaries and cross
compilation are not configured.

Install Xcode Command Line Tools (`xcode-select --install`) and Homebrew, then:

```sh
brew install python@3.12 dotnet make cmake ninja pkgconf \
  shaderc vulkan-headers vulkan-loader molten-vk
export PATH="$(brew --prefix python@3.12)/bin:$PATH"
```

The .NET 8+ **SDK** builds the managed IL2CPP and BKTR tools. GNU Make 4.3+ is
required for grouped targets; use Homebrew's `gmake`, since Apple's bundled Make
is too old. The converter selects `gmake` automatically on macOS.

Vulkan Memory Allocator is a header dependency. Use the SDK's VMA header through
`CPATH`, or install this source-only version into the ignored local prefix:

```sh
mkdir -p deps/vma/include
curl --fail --location \
  https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.0.1/include/vk_mem_alloc.h \
  -o deps/vma/include/vk_mem_alloc.h
```

The checked-in [VMA notice](../licenses/VulkanMemoryAllocator.txt) applies to
this header. No Homebrew formula for VMA is assumed. `pkg-config --cflags --libs
vulkan shaderc` must resolve the matching development libraries. A Vulkan SDK can
also supply them after its environment is configured.

For Homebrew MoltenVK, explicitly selecting its ICD is useful when another SDK
or driver configuration is present:

```sh
export VK_DRIVER_FILES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"
```

## Convert and run

From the repository root:

```sh
python3.12 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/requirements.txt
python3 tools/convert.py --target macos \
  --game "/path/to/extracted/update" \
  --output "$HOME/Games/Shadow Fight 2"
"$HOME/Games/Shadow Fight 2/sf2"
```

The required game layout and supported version are the same as the
[main guide](../README.md#getting-started). To convert directly from the supported
combined NSP, replace `--game` with `--nsp "/path/to/game.nsp" --keys
"/path/to/prod.keys"`. Native hactool/NSTool builds go into
`build/tools/extraction/macos` and `tools/bin/macos`; Linux ELF executables are
never reused as Mac tools. IL2CPP and BKTR run as managed .NET assemblies.

Conversion generates ARM SIMD assembly on Apple Silicon and portable C on Intel.
It builds `build/macos/sf2` and packages a folder with the game inputs, config,
fonts and minimal LGPL-only FFmpeg decoder. Local SDL `.dylib` files and FFmpeg
libraries are relocated to refer to their bundled copies, then ad-hoc signed.
Their original source install prefixes are not needed for those bundled files.
Vulkan loader, MoltenVK, shaderc and any system-supplied SDL remain dependencies
of the Homebrew/SDK installation used for compilation.

Keep the output together and in a writable location. Settings, saves and caches
live beside `sf2`; reconversion preserves settings and saves. This produces a
Terminal-launched folder for the host architecture. It does not produce a Finder
`.app`, notarized release or `.dmg`. `--install-desktop` is Linux-only.

## Development checks

```sh
gmake TARGET=macos macos-smoke
gmake TARGET=macos runtime-objs -j4
gmake TARGET=macos -j4              # after generating your own game code
```

The smoke gate needs no game files or GPU. It checks collision-safe memory
reservation, reset/protection, safe reads, threads, monotonic waits, 128-bit
atomics, Clang tail calls, Unicode paths and child-process arguments/status.
To check the native extraction tools, pinned FFmpeg decoder and Mach-O library
relocation locally:

```sh
python3 tools/build_extraction_tools.py --target macos --jobs 4
python3 tools/setup_ffmpeg.py --target macos --ensure
python3 tests/macos_dylibs_smoke.py
```

`tools/setup_runtime.sh --ensure` prepares missing SDL 3.4.16, SMAA and fallback
fonts, then compiles/links a dependency probe. `--check` verifies existing files
without downloads. SDL uses `deps/macos`; FFmpeg uses `deps/ffmpeg-macos`.
`SDL_DIR`, `FFMPEG_DIR`, `PYTHON`, `SF2_DEPS_WORK` and the offline source overrides
described in the [dependency guide](../tools/runtime-dependencies.md) remain
available. Set `MACOS_MIN` to change the default 13.0 compilation target; all
installed dependencies must also support the intended macOS version.

Hardware validation should cover boot, intro audio/video and skipping, menus,
combat, saves, resize/fullscreen and shutdown. MoltenVK feature differences,
shader translation and rendering correctness remain unverified.
