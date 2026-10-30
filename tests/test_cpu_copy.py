"""CPU buffer copies preserve staging snapshots and mapped-heap synchronization."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CpuCopyTests(unittest.TestCase):
    def test_copy_cache_and_synchronization_guards(self):
        source = (ROOT / "runtime/nvn/vulkan/exec.c").read_text()

        def function(signature):
            start = source.index(signature)
            end = source.index("\n}", start) + 2
            return source[start:end]

        prelude = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stdlib.h>
        #include <string.h>
        typedef struct {void *ptr;} VkSpan;
        static uintptr_t gmem_buf;
        static bool gc_lazy, overlap=true;
        static const char *wait_why;
        static unsigned flushes;
        static bool overlap_on(void) {return overlap;}
        static void renderer_flush(void) {++flushes;}
        static uint64_t vk_pending_seq(void) {return 1;}
        static uint64_t gmem_heap_size(void) {return 0;}
        static bool gc_scan_needed(void) {return false;}
        static void gc_scan_pointers(const uint32_t *p,uint64_t size) {(void)p;(void)size;}
        static uint64_t prof_scan_skipped;
        static VkSpan vk_transient_copy(const void *data,uint64_t size,uint64_t align) {
         (void)align;void *p=malloc(size);assert(p);memcpy(p,data,size);return (VkSpan){p};
        }
        '''
        start = source.index("#define GCOPY_TAB")
        end = source.index("// scan:", start)
        code = prelude + source[start:end]
        code += function("static VkSpan guest_copy_ex(")
        code += function("static void copy_buffer_to_buffer(")
        code += r'''
        int main(void) {
         char old[16]="0123456789abcde", dst[16], src[16]="fedcba987654321";
         memcpy(dst,old,sizeof dst);
         VkSpan before=guest_copy_ex((uintptr_t)dst,sizeof dst,false);
         assert(!memcmp(before.ptr,old,sizeof dst));
         copy_buffer_to_buffer((uintptr_t)src,(uintptr_t)dst,sizeof dst);
         assert(flushes==0);
         VkSpan after=guest_copy_ex((uintptr_t)dst,sizeof dst,false);
         assert(after.ptr!=before.ptr && !memcmp(after.ptr,src,sizeof dst));
         assert(!memcmp(before.ptr,old,sizeof dst));
         // An overlapping memmove must also discard cached snapshots of the destination.
         char expected[16];memcpy(expected,dst,16);memmove(expected+1,expected,12);
         copy_buffer_to_buffer((uintptr_t)dst,(uintptr_t)(dst+1),12);
         VkSpan moved=guest_copy_ex((uintptr_t)dst,sizeof dst,false);
         assert(!memcmp(moved.ptr,expected,16) && flushes==0);
         gmem_buf=1;copy_buffer_to_buffer((uintptr_t)src,(uintptr_t)dst,16);assert(flushes==1);
         gmem_buf=0;gc_lazy=true;copy_buffer_to_buffer((uintptr_t)src,(uintptr_t)dst,16);assert(flushes==2);
         gc_lazy=false;overlap=false;copy_buffer_to_buffer((uintptr_t)src,(uintptr_t)dst,16);assert(flushes==3);
         assert(!memcmp(dst,src,16));
         free(before.ptr);free(after.ptr);free(moved.ptr);
        }
        '''
        with tempfile.TemporaryDirectory(prefix="sf2-copy-") as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["clang", "-O2", "-Wall", "-Wextra", str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
