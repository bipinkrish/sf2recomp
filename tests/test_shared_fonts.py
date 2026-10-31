"""Shared-font requests must complete with mapped font data, including after reset."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("font_setup_tests", ROOT / "tools/setup_fonts.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class SharedFontTests(unittest.TestCase):
    def test_service_loaded_state_data_ranges_and_reset(self):
        source = r'''
#include <assert.h>
#include <stdarg.h>
#include "services/pl.c"
static char executable[4096];
static KSharedMem objects[8];
static unsigned handles;
bool plat_exe_path(char *out, size_t n) { return snprintf(out, n, "%s", executable) < n; }
void rt_fatal_msg(const char *fmt, ...) { va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); }
void rt_fatal_trace(void) { abort(); }
KSharedMem *kernel_create_shared_memory(uint64_t size, void (*map)(KSharedMem *), uint32_t *handle) {
    assert(handles < 8); *handle = ++handles;
    KSharedMem *s = &objects[handles - 1]; s->size = size; s->on_map = map; return s;
}
static IpcResp call(uint32_t cmd, uint32_t type) {
    IpcReq req = {.cmd=cmd, .data=(const uint8_t *)&type, .data_size=4};
    IpcResp resp = {0}; pl_user(NULL, &req, &resp); return resp;
}
int main(int argc, char **argv) {
    assert(argc == 2); snprintf(executable, sizeof executable, "%s", argv[1]);
    assert(!fonts[0].data && !fonts[1].data);
    for (uint32_t type=0; type<FONT_TYPES; ++type) {
        // Unity's first request is a poll, before RequestLoad. It must return
        // loaded only after both files have actually been made available.
        IpcResp r = call(1, type);
        assert(!r.result && r.data_size==4 && *(uint32_t *)r.data==1);
        assert(fonts[0].data && fonts[1].data);
        r=call(0,type); assert(!r.result);
        SharedFont *font=font_for_type(type);
        r=call(2,type); assert(!r.result && *(uint32_t *)r.data==font->size);
        r=call(3,type); assert(!r.result && *(uint32_t *)r.data==font->offset);
        assert(font->offset + font->size <= FONT_MEMORY_SIZE);
    }
    assert(fonts[0].offset+fonts[0].size <= fonts[1].offset);
    IpcResp r=call(4,0); assert(!r.result && r.ncopy==1);
    uint32_t h=r.copy_handles[0]; assert(h==1);
    KSharedMem *mem=&objects[h-1]; assert(mem->size==FONT_MEMORY_SIZE);
    uint8_t *mapping=calloc(1,mem->size); mem->guest_addr=(uintptr_t)mapping; mem->on_map(mem);
    for (unsigned i=0;i<2;++i) assert(!memcmp(mapping+fonts[i].offset,fonts[i].data,fonts[i].size));
    r=call(4,0); assert(r.copy_handles[0]!=h); // caller can close its own handle

    uint32_t types[3]={0,0,0xdeadbeef},offsets[3]={0,0,0xdeadbeef},sizes[3]={0,0,0xdeadbeef};
    IpcReq req={.cmd=5,.nb=3,.b={{(uintptr_t)types,8},{(uintptr_t)offsets,8},{(uintptr_t)sizes,8}}};
    memset(&r,0,sizeof r); pl_user(NULL,&req,&r);
    assert(!r.result && r.data[0]==1 && *(uint32_t *)(r.data+4)==2);
    assert(types[0]==0 && types[1]==1 && offsets[1]==fonts[1].offset && sizes[0]==fonts[0].size);
    assert(types[2]==0xdeadbeef && offsets[2]==0xdeadbeef && sizes[2]==0xdeadbeef);
    req.b[1].size=4; memset(&r,0,sizeof r); pl_user(NULL,&req,&r);
    assert(*(uint32_t *)(r.data+4)==1); // smallest output buffer bounds all three lists
    r=call(1,6); assert(r.result);
    r=call(99,0); assert(r.result);
    req=(IpcReq){.cmd=1}; memset(&r,0,sizeof r); pl_user(NULL,&req,&r); assert(r.result);
    free(mapping);
    reset_shared_fonts(); assert(!fonts[0].data && !fonts[1].data);
    r=call(1,0); assert(!r.result && *(uint32_t *)r.data==1);
    reset_shared_fonts(); return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-shared-font-") as folder:
            root = Path(folder)
            fonts = root / "lib/fonts"
            fonts.mkdir(parents=True)
            (fonts / "NotoSans-Regular.ttf").write_bytes(b"\0\1\0\0" + bytes(range(64)))
            (fonts / "NotoSansCJKsc-Regular.otf").write_bytes(b"OTTO" + bytes(range(100)))
            (root / "test.c").write_text(source)
            subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"), str(root / "test.c"),
                            "-pthread", "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test"), str(root / "test")], check=True)

    def test_setup_pins_offline_files_and_preserves_installed_fonts_on_failure(self):
        with tempfile.TemporaryDirectory(prefix="sf2-font-setup-") as folder:
            root = Path(folder)
            source, output = root / "source", root / "output"
            source.mkdir()
            content = b"fixture font and license"
            (source / "font.ttf").write_bytes(content)
            files = {"font.ttf": ("https://unused.example/font.ttf", hashlib.sha256(content).hexdigest())}
            with patch.object(setup, "FILES", files), patch.dict(os.environ, SF2_FONT_SOURCE_DIR=str(source)):
                setup.prepare(output)
                setup.prepare(output, check=True)
                stamp = (output / "font.ttf").stat().st_mtime_ns
                setup.prepare(output)
                self.assertEqual(stamp, (output / "font.ttf").stat().st_mtime_ns)
                # A failed repair must leave existing files intact.
                (output / "font.ttf").write_bytes(b"existing file")
                (source / "font.ttf").write_bytes(b"bad offline input")
                with self.assertRaisesRegex(ValueError, "Font checksum mismatch"):
                    setup.prepare(output)
                self.assertEqual((output / "font.ttf").read_bytes(), b"existing file")
                with self.assertRaisesRegex(ValueError, "Bundled fonts missing or changed"):
                    setup.prepare(output, check=True)


if __name__ == "__main__":
    unittest.main()
