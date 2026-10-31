"""Driver-owned heaps must preserve shader access and never fall back to userptr imports."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ManagedHeapTests(unittest.TestCase):
    def test_managed_mode_never_requests_userptr_chunks_or_probe(self):
        source = (ROOT / "runtime/nvn/vulkan/device.c").read_text()
        start = source.index("static bool lazy_heap_predicted(")
        function = source[start:source.index("\n}", start) + 2]
        harness = r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HEAP_MAX 0xd0000000ull
static struct {bool managed_heap,dmabuf,map_placed;VkPhysicalDevice phys;VkPhysicalDeviceProperties props;} g_vk;
static bool whole_import_fits(bool budget,bool *probe) {
    (void)budget;(void)probe;assert(!"managed mode must not probe a userptr import");return false;
}
void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice device,VkPhysicalDeviceMemoryProperties *properties) {
    (void)device;(void)properties;assert(!"unexpected memory query");
}
'''
        harness += function
        harness += r'''
int main(void) {
    bool probe=true;
    g_vk.managed_heap=true;
    assert(!lazy_heap_predicted(true,&probe) && !probe);
    // The generic diagnostic override cannot silently re-enable userptr imports in managed mode.
    setenv("LARECOMP_VK_GMEM_LAZY","1",1);probe=true;
    assert(!lazy_heap_predicted(true,&probe) && !probe);
    g_vk.managed_heap=false;
    assert(lazy_heap_predicted(true,&probe) && !probe);
    unsetenv("LARECOMP_VK_GMEM_LAZY");setenv("LARECOMP_VK_NO_GMEM","1",1);
    assert(!lazy_heap_predicted(true,&probe) && !probe);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-managed-policy-") as directory:
            work = pathlib.Path(directory)
            (work / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-Wall", str(work / "test.c"), "-o", str(work / "test")], check=True)
            subprocess.run([str(work / "test")], check=True)

    def test_prepare_failure_is_fatal_and_success_keeps_global_memory(self):
        source = (ROOT / "runtime/nvn/vulkan/exec.c").read_text()

        def function(signature):
            start = source.index(signature)
            return source[start:source.index("\n}", start) + 2]

        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
static struct { bool managed_heap, host_import; int dev; unsigned vendorID; struct {unsigned vendorID;} props; } g_vk;
static bool gmem_userptr, gmem_dmabuf, gc_lazy, allocation_ok;
static uintptr_t gmem_buf;
static uint64_t gmem_size, heap_size=0x100000;
static int tracking_off, allocations, imports;
static jmp_buf failure;
#define FATAL(...) longjmp(failure,1)
#define LOG(...) ((void)0)
typedef struct {uintptr_t buffer; uint64_t offset,range;} VkDescriptorBufferInfo;
typedef struct {uintptr_t buf;uint64_t off;} VkSpan;
static bool guest_heap_dmabuf(void) {
    allocations++;
    if (allocation_ok) {gmem_dmabuf=true;gmem_buf=123;gmem_size=heap_size;}
    return allocation_ok;
}
static void gmem_chunk_shift(void) {}
static void plat_write_tracking_off(const char *why) {(void)why;tracking_off++;}
static uint64_t gmem_heap_size(void) {return heap_size;}
static bool gmem_import(uint64_t size) {(void)size;imports++;return false;}
static VkSpan vk_transient_copy(const void *p,uint64_t size,uint64_t align) {
    (void)p;(void)size;(void)align;assert(!"managed heap must not become the zero-buffer fallback");return (VkSpan){0};
}
'''
        harness += function("void backend_guest_heap_prepare(")
        harness += function("static VkDescriptorBufferInfo gmem_info(")
        harness += r'''
int main(void) {
    g_vk.managed_heap=true;g_vk.dev=1;g_vk.host_import=false;g_vk.props.vendorID=0x1002;
    // The original fallback silently entered the hazardous import path after allocation failure.
    if (!setjmp(failure)) {backend_guest_heap_prepare();assert(!"failure must stop boot");}
    assert(allocations==1 && !gmem_userptr && !imports);
    allocation_ok=true;
    backend_guest_heap_prepare();
    assert(allocations==2 && tracking_off==1 && !gmem_userptr);
    // Disabling the import extension must still bind real global shader memory, including a grown guest heap.
    VkDescriptorBufferInfo bi=gmem_info();assert(bi.buffer==123 && bi.range==heap_size && !imports);
    heap_size/=2;bi=gmem_info();assert(bi.buffer==123 && bi.range==gmem_size && !imports);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-managed-") as directory:
            work = pathlib.Path(directory)
            (work / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-Wall", str(work / "test.c"), "-o", str(work / "test")], check=True)
            subprocess.run([str(work / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
