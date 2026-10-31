# Native runtime dependencies

This dependency recipe targets **Linux x86_64**. The separate
[experimental Windows guide](../docs/windows.md) covers the complete native
Windows conversion/build workflow. The Linux commands here do not produce a
Windows executable. Game extraction and CPU-code
generation are separate steps: no proprietary game files or generated game code
are supplied by this setup script.

The [experimental macOS guide](../docs/macos.md) covers Apple Silicon and Intel
Macs. The shared setup script also supports Darwin, using local `.dylib` SDL,
the system linker and `libSystem`, plus Vulkan/MoltenVK and shaderc development
packages. Its pinned SDL/SMAA/font sources are the same as on Linux.

| Dependency | Used for | Supported / checked here |
|---|---|---|
| Clang / Clang++, LLD, GNU Make | Compile and link translated C and runtime C/C++ | Clang/LLD 18 checked; generated calls require Clang's `musttail` |
| SDL3 | Window, input, audio, captures | 3.4.0+; source build pinned to 3.4.16 |
| Noto Sans / Noto Sans CJK | Shared-font service fallback | Pinned upstream font files, SIL OFL 1.1; prepared automatically |
| Vulkan headers and loader | Graphics API | Vulkan 1.3+ |
| shaderc development library | Compile translated GPU shaders to SPIR-V | System shaderc; 2023.8 checked locally |
| Vulkan Memory Allocator headers | GPU memory allocation | VMA 3.x; 3.0.1 package checked locally |
| C/C++ development libraries, pthread, libatomic, libm, libdl | Host runtime | Supplied by Linux development packages |
| pkg-config | Locate installed development packages | Needed for dependency check and build |
| CMake, curl, SHA256 utilities, tar | Fetch/build pinned SDL source | Only needed for `--build-sdl` |

SDL 3.2 is insufficient: the capture path uses `SDL_SavePNG`, introduced in
[SDL 3.4.0](https://wiki.libsdl.org/SDL3/SDL_SavePNG). The script checks both
headers and the linked SDL version. VMA is a header library, compiled once in
`runtime/nvn/vulkan/vma.cpp`; install its development headers even when Vulkan
itself is already installed.

## Install development packages

These are package-name examples, not a system installer. Availability varies by
distribution release. The SDL source build also needs an installed display/audio
backend; this X11/ALSA/PulseAudio subset supports the currently verified launch
path. Optional Wayland, PipeWire and controller backends can be enabled by adding
their development packages before configuring SDL; see the
[official SDL Linux dependency list](https://wiki.libsdl.org/SDL3/README-linux).

Ubuntu 24.04+ example (`universe` provides the graphics development packages):

```sh
sudo apt install build-essential clang lld make cmake ninja-build pkg-config curl ca-certificates \
  libvulkan-dev libshaderc-dev libvulkan-memory-allocator-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libasound2-dev libpulse-dev libudev-dev
```

The Vulkan allocator and shader compiler names are documented by
[Ubuntu's VMA package](https://packages.ubuntu.com/libvulkan-memory-allocator-dev)
and [shaderc package](https://packages.ubuntu.com/libshaderc-dev).

Fedora example:

```sh
sudo dnf install gcc-c++ clang lld make cmake ninja-build pkgconf-pkg-config curl ca-certificates \
  vulkan-loader-devel vulkan-headers libshaderc-devel VulkanMemoryAllocator-devel \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel \
  libXi-devel libXScrnSaver-devel libXtst-devel alsa-lib-devel \
  pulseaudio-libs-devel systemd-devel
```

Package references: [Vulkan loader](https://packages.fedoraproject.org/pkgs/vulkan-loader/vulkan-loader-devel/),
[shaderc](https://packages.fedoraproject.org/pkgs/shaderc/),
[VMA](https://packages.fedoraproject.org/pkgs/VulkanMemoryAllocator/VulkanMemoryAllocator-devel/index.html).
The Fedora recipe has not been executed in this workspace. Header locations must
match `<vma/vk_mem_alloc.h>` or `<vk_mem_alloc.h>`; `CPATH` can point to a custom
VMA include prefix when necessary.

A matching Vulkan driver/ICD is needed to **run**, separately from these build
packages. `mesa-vulkan-drivers` supplies Mesa drivers on Ubuntu/Fedora; proprietary
GPUs may need their vendor driver. A graphical X11/Xwayland session is needed
for the currently verified software-rendering launcher. Installing development
packages alone does not validate GPU compatibility.

## Check existing dependencies

From the repository root:

```sh
tools/setup_runtime.sh --fetch-smaa  # once, if using a system/custom SDL install
tools/setup_runtime.sh --check
```

This compiles, links and runs a small SDL/Vulkan-loader/shaderc/VMA probe. It does
not open a window, initialize a GPU device, download files, or install packages.
It prefers a complete local SDL prefix in `deps`, then a system `sdl3` pkg-config
package. Use `SDL_DIR=/path/to/prefix` for a custom local installation.

## Build SDL locally from pinned source

```sh
tools/setup_runtime.sh --build-sdl
make -j4
```

The script follows SDL's [CMake source-build workflow](https://wiki.libsdl.org/SDL3/README-cmake),
builds a shared library, and installs into the repository's `deps` prefix without
sudo. Source/build caches go under ignored `build/dependencies`. `JOBS=8` changes
parallelism. `SDL_DIR=/path/to/prefix` and `SF2_DEPS_WORK=/path/to/cache` relocate
outputs; pass the same `SDL_DIR` to Make when using a custom prefix.

The pin matches the SDL version used for this port's current validation, rather
than following the latest release automatically:

- Release: [SDL 3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16).
- Archive: `https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz`.
- SHA256: `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`,
  published in the [official release asset listing](https://github.com/libsdl-org/SDL/releases?expanded=true).

For an offline build, download that exact source archive elsewhere, then set
`SDL_SOURCE_ARCHIVE=/path/to/SDL3-3.4.16.tar.gz`. The same checksum is required.
The script copies SDL's license and an archive provenance record into
`deps/share/licenses/SDL3`. It fetches source only and builds locally; no SDL
binary is downloaded or committed.

CMake's configuration summary should show the display/audio backends you need.
If dependencies were missing during a prior configuration, install them and
rerun the script. This pins SDL source, not every compiler/system-library version;
it is a reproducible dependency recipe, not a promise of byte-identical builds.

## Bundled cutscene decoder

`tools/convert.py` builds a minimal FFmpeg 9.0.1 decoder from its checksum-pinned
upstream source when needed, then includes the executable, shared libraries,
source archive and license notices in the converted game folder. Players do not
need FFmpeg installed or available on `PATH`. Building the package needs Clang,
Make, `tar`, `curl` and a network connection for the pinned source archive; for
offline setup, set `FFMPEG_SOURCE_ARCHIVE` to that exact archive. `FFMPEG_DIR`
and `SF2_DEPS_WORK` can relocate the install prefix and source cache. See
`python3 tools/setup_ffmpeg.py --help-env` for the override summary.

## Validation in this workspace

The dependency probe passed with the existing SDL 3.4.16 installation and with a
fresh shared SDL 3.4.16 built from the checksum-verified release archive into an
isolated temporary prefix. The local build enabled X11, Wayland, ALSA,
PulseAudio and Vulkan. Checksum rejection was checked with an invalid archive.
No distribution packages were installed by the script or this verification.

## SMAA source setup

The root `third_party/` folder and `deps/include/SDL3/` are ignored local
dependencies, not vendored source. SDL's installation supplies its headers.
`--build-sdl` also prepares SMAA; `--fetch-smaa` prepares it separately without
building SDL or checking system libraries. A direct `make` and conversion use `--ensure` to fetch missing SMAA and build
pinned SDL when no complete local/system SDL 3.4+ is available. `--check` never
downloads and verifies the local SMAA files.

`tools/setup_smaa.py` downloads four files from official
`iryoku/smaa` revision `71c806a838bdd7d517df19192a20f0c61b3ca29d`, checks each
SHA256, then generates the shader C include locally. The pinned files match the
previously vendored shader and tables. The generated include is also excluded
from Git. Subsequent builds use the local files without network access.
For offline setup, set `SMAA_SOURCE_DIR` to a folder containing the pinned
`SMAA.hlsl`, `LICENSE.txt`, `Textures/AreaTex.h` and `Textures/SearchTex.h` files;
the same checksum checks apply. The SMAA license remains tracked as
`licenses/SMAA.txt` and is copied into converted output with the other notices.

## Shared fallback fonts

`tools/setup_fonts.py` downloads checksum-pinned Noto Sans and Noto Sans CJK
files and their SIL OFL 1.1 licenses into `deps/share/fonts`. `--ensure` and
`--build-sdl` prepare these automatically; `--check` verifies without downloads.
The exact revisions, URLs and SHA256 values are recorded in the setup script and
the generated `SOURCE.txt`. Set `SF2_FONT_SOURCE_DIR` to a folder containing the
four pinned files for offline setup; checksums still apply.

`make` copies verified files into `<build>/lib/fonts`; conversion includes the
same folder and `licenses/Noto` in its output. The font service loads them lazily
when requested and maps their bytes through the regular shared-memory API.
The standard and CJK faces are substitutes, not copies of console firmware
fonts. The CJK face covers Chinese, Japanese and Korean; Nintendo-specific
private-use icon glyphs are not supplied by the standard fallback face.

## Distribution

Source releases should contain the runtime/compiler sources and required license
notices. Users supply their own game input and generate translated code, shader
caches, assets and executables locally. These generated files and the local SDL
installation are build outputs, not repository source inputs. If distributing
host libraries, include their required notices and account for their runtime
library dependencies; the local prefix alone is not a portable binary package.
