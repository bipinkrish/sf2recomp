"""Background startup imports preserve renderer ownership and reset lifetimes."""
from source_values import read_source
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StartupPreloadTests(unittest.TestCase):
    def test_async_import_adoption_fallback_and_reset(self):
        source = read_source(ROOT / "runtime/nvn/vulkan/exec.c")
        start = source.index("static struct {\n    pthread_t thread;", source.index("static bool whole_import_probe_oom"))
        end = source.index("// Linux, AMD (RADV)", start)
        harness = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
typedef uint64_t VkBuffer;
typedef uint64_t VkDeviceMemory;
#define VK_NULL_HANDLE 0
#define LOG(...) ((void)0)
static struct { bool host_import, managed_heap; int dev; } g_vk = {.host_import = true};
static bool gc_lazy;
static VkBuffer gmem_buf;
static VkDeviceMemory gmem_mem;
static uint64_t gmem_size, heap_size = 4096;
static atomic_bool release_import, entered;
static atomic_uint imports, destroyed, freed;
static bool fail_import;
static pthread_t render_thread;
static uint64_t plat_now_ns(void) { return 1; }
static uint64_t gmem_heap_size(void) { return heap_size; }
static uint32_t gmem_chunk_shift(void) { return 0; }
static bool host_import(uint64_t off, uint64_t size, VkBuffer *buf, VkDeviceMemory *mem, bool bda) {
    assert(!off && size == heap_size && !bda);
    atomic_fetch_add(&imports, 1);
    atomic_store(&entered, true);
    while (!atomic_load(&release_import)) sched_yield();
    if (fail_import) return false;
    *buf = 100; *mem = 200;
    return true;
}
static void vkDestroyBuffer(int dev, VkBuffer buf, void *allocator) {
    assert(pthread_equal(pthread_self(), render_thread) && buf == 100);
    atomic_fetch_add(&destroyed, 1);
}
static void vkFreeMemory(int dev, VkDeviceMemory mem, void *allocator) {
    assert(pthread_equal(pthread_self(), render_thread) && mem == 200);
    atomic_fetch_add(&freed, 1);
}
static void vk_defer_buffer(VkBuffer buf, VkDeviceMemory mem) { assert(false); }
''' + source[start:end] + r'''
static void start(void) {
    atomic_store(&release_import, false); atomic_store(&entered, false);
    vk_guest_heap_preload();
    while (!atomic_load(&entered)) sched_yield();
    // The start returned while the import remains blocked on another thread.
    assert(gmem_preload.started && !gmem_buf);
}
int main(void) {
    render_thread = pthread_self();
    start();
    vk_guest_heap_preload();
    assert(imports == 1); // multiple movie frames cannot start duplicate imports
    atomic_store(&release_import, true);
    assert(gmem_import(heap_size));
    assert(imports == 1 && gmem_buf == 100 && gmem_mem == 200 && gmem_size == heap_size);
    vk_guest_heap_preload_end();
    assert(!destroyed && !freed); // the adopted handles belong to the renderer
    gmem_buf = gmem_mem = gmem_size = 0;

    start(); atomic_store(&release_import, true);
    vk_guest_heap_preload_end();
    assert(destroyed == 1 && freed == 1 && !gmem_preload.started);
    vk_guest_heap_preload_end();
    assert(destroyed == 1 && freed == 1); // reset discards unused handles once

    start();
    // Simulate growth after preparation: stale handles cannot cover the heap.
    heap_size = 8192; atomic_store(&release_import, true);
    assert(gmem_import(heap_size) && imports == 4);
    assert(destroyed == 2 && freed == 2 && gmem_size == 8192);
    vk_guest_heap_preload_end();
    gmem_buf = gmem_mem = gmem_size = 0;

    fail_import = true;
    start(); atomic_store(&release_import, true);
    assert(!gmem_import(heap_size)); // failed preparation uses the normal fallback
    assert(imports == 6 && !gmem_buf);
    vk_guest_heap_preload_end(); fail_import = false;
    unsigned before = imports;
    g_vk.managed_heap = true; vk_guest_heap_preload(); g_vk.managed_heap = false;
    g_vk.host_import = false; vk_guest_heap_preload(); g_vk.host_import = true;
    gc_lazy = true; vk_guest_heap_preload(); gc_lazy = false;
    setenv("SF2RECOMP_VK_NO_GMEM", "1", 1); vk_guest_heap_preload(); unsetenv("SF2RECOMP_VK_NO_GMEM");
    heap_size = 0; vk_guest_heap_preload(); heap_size = 4096;
    gmem_buf = 1; vk_guest_heap_preload(); gmem_buf = 0;
    assert(imports == before && !gmem_preload.started);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-startup-preload-") as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", str(path / "test.c"), "-pthread", "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
