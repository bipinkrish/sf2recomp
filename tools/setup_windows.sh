#!/usr/bin/env bash
# Native Windows dependencies. Run from MSYS2 UCRT64 with its Clang toolchain.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:---check}
[[ $# -le 1 && ( $mode == --check || $mode == --ensure || $mode == --fetch-smaa || $mode == --help ) ]] || {
    echo "Usage: bash tools/setup_windows.sh [--check|--ensure|--fetch-smaa|--help]" >&2; exit 2;
}
if [[ $mode == --help ]]; then
    echo "Use MSYS2 UCRT64, Clang, SDL >=3.4, Vulkan >=1.3, shaderc, VMA and winpthreads."
    echo "--ensure prepares pinned SMAA/fonts; --check only verifies. See docs/windows.md."
    exit
fi
[[ ${MSYSTEM:-} == UCRT64 && $(uname -m) == x86_64 ]] || {
    echo "Windows builds require the MSYS2 UCRT64 shell on Windows x64 (see docs/windows.md)." >&2; exit 1;
}
for tool in clang clang++ ld.lld make pkg-config windres; do
    command -v "$tool" >/dev/null || { echo "Missing Windows build tool: $tool" >&2; exit 1; }
done
python=${PYTHON:-python3}
if [[ $mode == --fetch-smaa ]]; then exec "$python" "$root/tools/setup_smaa.py"; fi
if [[ $mode == --ensure ]]; then
    "$python" "$root/tools/setup_smaa.py"
    "$python" "$root/tools/setup_fonts.py"
else
    "$python" "$root/tools/setup_smaa.py" --check
    "$python" "$root/tools/setup_fonts.py" --check
fi
pkg-config --atleast-version=3.4.0 sdl3 || { echo "SDL 3.4+ development files required." >&2; exit 1; }
pkg-config --atleast-version=1.3 vulkan || { echo "Vulkan 1.3+ development files required." >&2; exit 1; }
read -r -a flags <<< "$(pkg-config --cflags --libs sdl3 vulkan shaderc)"
probe=$(mktemp -d)
trap 'rm -rf -- "$probe"' EXIT
cat > "$probe/compiler.c" <<'C'
#if !defined(_WIN32) || !defined(__x86_64__) || !defined(__clang__) || !defined(__MINGW32__)
#error Use the native Windows x64 Clang toolchain from MSYS2 UCRT64
#endif
#include <stdint.h>
#include <pthread.h>
// A tail call needs the same signature. Exercise the generated-code contract.
static void __attribute__((noinline)) target(void *p) { *(volatile int *)p = 1; }
void tail(void *p) { __attribute__((musttail)) return target(p); }
_Alignas(16) static __uint128_t value;
int main(void) {
    __uint128_t expected = 0, desired = 1;
    return !__atomic_compare_exchange(&value, &expected, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
C
clang -O1 -mcx16 "$probe/compiler.c" -o "$probe/compiler.exe" -pthread
"$probe/compiler.exe"
cat > "$probe/deps.cpp" <<'CPP'
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif
static_assert(SDL_VERSION_ATLEAST(3, 4, 0));
static_assert(VK_HEADER_VERSION_COMPLETE >= VK_MAKE_API_VERSION(0, 1, 3, 0));
int main() {
    auto c = shaderc_compiler_initialize();
    if (!c || SDL_GetVersion() < SDL_VERSIONNUM(3, 4, 0)) return 1;
    shaderc_compiler_release(c);
    uint32_t version = 0;
    return vkEnumerateInstanceVersion(&version) != VK_SUCCESS;
}
CPP
clang++ -std=c++17 "$probe/deps.cpp" -o "$probe/deps.exe" "${flags[@]}"
"$probe/deps.exe"
echo "Windows compiler and runtime dependencies passed."
