#!/usr/bin/env bash
# Check native dependencies, or prepare pinned SDL/SMAA sources locally.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:---check}
if (( $# > 1 )) || [[ $mode != --check && $mode != --build-sdl && $mode != --fetch-smaa && $mode != --help ]]; then
    echo "Usage: $0 [--check|--build-sdl|--fetch-smaa|--help]" >&2
    exit 2
fi
if [[ $mode == --help ]]; then
    cat <<'HELP'
--check      Compile/link native dependency probe; no downloads (default).
--build-sdl  Prepare SMAA, build/install verified SDL 3.4.16 locally, then check.
--fetch-smaa Download verified SMAA sources and generate its shader include only.
SDL_DIR      Installation prefix (default: repo/deps). No sudo is used.
JOBS         Parallel SDL compilation jobs (default: 4).
SDL_SOURCE_ARCHIVE  Optional local SDL3-3.4.16.tar.gz; same SHA256 required.
SF2_DEPS_WORK      Source/build cache (default: repo/build/dependencies).
SMAA_SOURCE_DIR    Optional offline SMAA source folder; same checksums required.
See tools/runtime-dependencies.md for system packages and licenses.
HELP
    exit
fi
if [[ $mode == --fetch-smaa ]]; then
    exec python3 "$root/tools/setup_smaa.py"
fi
if [[ $mode == --build-sdl ]]; then
    python3 "$root/tools/setup_smaa.py"
else
    python3 "$root/tools/setup_smaa.py" --check
fi
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    echo "The current runtime build targets Linux x86_64." >&2; exit 1;
}
for command in clang clang++ ld.lld make pkg-config; do
    command -v "$command" >/dev/null || { echo "Missing tool: $command" >&2; exit 1; }
done
pkg-config --atleast-version=1.3 vulkan || { echo "Install Vulkan 1.3+ development files." >&2; exit 1; }
pkg-config --exists shaderc || { echo "Install shaderc development files (pkg-config shaderc)." >&2; exit 1; }
prefix=${SDL_DIR:-"$root/deps"}
prefix=$(realpath -m -- "$prefix")
if [[ $mode == --build-sdl ]]; then
    for command in cmake curl sha256sum tar; do
        command -v "$command" >/dev/null || { echo "Missing SDL build tool: $command" >&2; exit 1; }
    done
    jobs=${JOBS:-4}
    [[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo "JOBS must be a positive integer." >&2; exit 2; }
    version=3.4.16
    sha256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    url="https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL3-$version.tar.gz"
    work=${SF2_DEPS_WORK:-"$root/build/dependencies"}
    mkdir -p -- "$work"
    work=$(realpath -- "$work")
    archive=${SDL_SOURCE_ARCHIVE:-"$work/SDL3-$version.tar.gz"}
    if [[ ! -f $archive ]]; then
        [[ ! ${SDL_SOURCE_ARCHIVE:-} ]] || { echo "Archive not found: $archive" >&2; exit 1; }
        curl --fail --location --proto '=https' --tlsv1.2 --connect-timeout 20 --max-time 180 --retry 2 "$url" -o "$archive.part"
        mv -- "$archive.part" "$archive"
    fi
    actual=$(sha256sum -- "$archive")
    [[ ${actual%% *} == "$sha256" ]] || { echo "SDL archive checksum mismatch: $archive" >&2; exit 1; }
    source_dir="$work/SDL3-$version"
    if [[ ! -f $source_dir/CMakeLists.txt ]]; then
        mkdir -p -- "$source_dir"
        tar -xzf "$archive" --strip-components=1 -C "$source_dir"
    fi
    cmake -S "$source_dir" -B "$work/sdl-$version-build" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build "$work/sdl-$version-build" --parallel "$jobs"
    cmake --install "$work/sdl-$version-build"
    mkdir -p -- "$prefix/share/licenses/SDL3"
    cp -- "$source_dir/LICENSE.txt" "$prefix/share/licenses/SDL3/LICENSE.txt"
    printf 'SDL %s\nURL %s\nSHA256 %s\n' "$version" "$url" "$sha256" > "$prefix/share/licenses/SDL3/SOURCE.txt"
fi

# Prefer a complete local SDL prefix; otherwise accept the system package.
sdl_flags=()
if [[ -f $prefix/include/SDL3/SDL.h && ( -f $prefix/lib/libSDL3.so || -f $prefix/lib/libSDL3.a ) ]]; then
    sdl_flags=("-I$prefix/include" "-L$prefix/lib" "-Wl,-rpath,$prefix/lib" -lSDL3)
    echo "SDL prefix: $prefix"
else
    pkg-config --atleast-version=3.4.0 sdl3 || {
        echo "SDL3 3.4+ missing. Run $0 --build-sdl or set SDL_DIR to its prefix." >&2; exit 1;
    }
    read -r -a sdl_flags <<< "$(pkg-config --cflags --libs sdl3)"
    echo "SDL system package: $(pkg-config --modversion sdl3)"
fi
read -r -a graphics_flags <<< "$(pkg-config --cflags --libs vulkan shaderc)"
probe=$(mktemp -d "${TMPDIR:-/tmp}/sf2-deps.XXXXXX")
trap 'rm -rf -- "$probe"' EXIT
cat > "$probe/check.cpp" <<'CPP'
#include <cstdio>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#elif __has_include(<vk_mem_alloc.h>)
#include <vk_mem_alloc.h>
#else
#error Install Vulkan Memory Allocator development headers (see runtime-dependencies.md)
#endif
#if !SDL_VERSION_ATLEAST(3, 4, 0)
#error SDL 3.4.0 or newer is required (SDL_SavePNG)
#endif
static_assert(VK_HEADER_VERSION_COMPLETE >= VK_MAKE_API_VERSION(0, 1, 3, 0), "Vulkan 1.3 headers required");
int main() {
    const int version = SDL_GetVersion();
    if (version < SDL_VERSIONNUM(3, 4, 0)) return 1;
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    if (!compiler) return 2;
    shaderc_compiler_release(compiler);
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    if (!vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance")) return 3;
    std::printf("SDL %d.%d.%d; Vulkan headers %u; shaderc and VMA available\n",
        SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), VK_HEADER_VERSION);
    return allocation.usage == VMA_MEMORY_USAGE_AUTO ? 0 : 4;
}
CPP
clang++ -std=c++17 -mcx16 -fuse-ld=lld "$probe/check.cpp" -o "$probe/check" \
    "${sdl_flags[@]}" "${graphics_flags[@]}" -latomic -pthread -lm -ldl
"$probe/check"
printf 'Native dependency check passed. Build with: make SDL_DIR="%s" -j%s\n' "$prefix" "${JOBS:-4}"
