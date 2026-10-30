"""A GPU import must materialize pages without changing live guest memory."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HostImportTests(unittest.TestCase):
    def test_pretouch_preserves_live_page_contents(self):
        source = (ROOT / "runtime/nvn/vulkan/exec.c").read_text()
        start = source.index("static void pretouch_host(")
        end = source.index("\n}", start) + 2
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define PLAT_PAGE 16384
'''
        harness += source[start:end]
        harness += r'''
int main(void) {
    const size_t size = 4 * PLAT_PAGE;
    uint8_t *p = mmap(0, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    uint8_t before[4 * PLAT_PAGE];
    for (size_t i = 0; i < size; ++i) before[i] = p[i] = (i * 17 + 0x60) & 255;
    // Constructor pointers and allocator headers can begin on a page boundary.
    *(uint64_t *)p = 0x7000d9a360ull;
    memcpy(before, p, size);
    pretouch_host(p, size);
    assert(!memcmp(before, p, size));
    pretouch_host(p + 7, size - 14);
    assert(!memcmp(before, p, size));
    pretouch_host(p, 0);
    assert(!memcmp(before, p, size));
    munmap(p, size);
    // The optimizer must retain the writes, and Linux can use smaller host pages
    // than the guest's 16 KiB granularity. Every untouched anonymous page must
    // become resident before the graphics driver pins this range.
    p = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    pretouch_host(p, size);
    unsigned char residency[64];
    assert(mincore(p, size, residency) == 0);
    for (size_t i = 0; i < size / (size_t)sysconf(_SC_PAGESIZE); ++i)
        assert(residency[i] & 1);
    munmap(p, size);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-import-") as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
