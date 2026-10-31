"""Half comparison forms used by the game's dialog fragment shader."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HalfCompareTests(unittest.TestCase):
    def test_dialog_constant_and_immediate_comparisons(self):
        source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvn/maxwell/translate.c"

static ShaderInfo *translate(uint64_t instruction) {
    uint64_t blob[24] = {0};
    ((uint32_t *)blob)[(0x30 + 0x48) / 4] = 15;
    blob[17] = instruction;
    blob[18] = 0xe30000000007000full; // EXIT
    blob[19] = 0xe2400fffff87000full; // BRA self (padding)
    ShaderKey key = {.glsl = 1};
    ShaderInfo *info = maxwell_translate((uint8_t *)blob, sizeof blob, STAGE_FRAGMENT, &key);
    if (!info->msl) fprintf(stderr, "%s\n", info->error);
    assert(info->msl && !info->error[0]);
    return info;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    // Exact instruction reported by the failed dialog shader: compare the low
    // half of R3 (replicated) against the f32 at c[1][0], writing predicate P0.
    ShaderInfo *s = translate(0x7e83038400070307ull);
    assert(s->cbuf_mask == 2 && s->cbuf_size[1] == 256);
    assert(strstr(s->msl, "hx(r3, 2u)"));
    assert(strstr(s->msl, "hx(cb1[0][0], 1u)"));
    assert(strstr(s->msl, "bool l = (hx_.x < hy_.x), h = (hx_.y < hy_.y)"));
    assert(strstr(s->msl, "p0 = l && pp"));
    FILE *f = fopen(argv[1], "w"); assert(f); fputs(s->msl, f); fclose(f);
    maxwell_free(s);

    // Constant HSET2: comparison/BF live in the high opcode fields; sign and
    // absolute modifiers must be applied to the broadcast constant operand.
    uint64_t w = 0x7c80000000070004ull | (2ull << 8) | (3ull << 34) |
                 (2ull << 20) | (2ull << 49) | (1ull << 53) |
                 (1ull << 54) | (1ull << 56) | (7ull << 39);
    s = translate(w);
    assert(s->cbuf_mask == 8);
    assert(strstr(s->msl, "(-abs(hx(cb3[0][2], 1u)))"));
    assert(strstr(s->msl, "hx_.x == hy_.x"));
    assert(strstr(s->msl, "r4 = (l ? 0x3c00u"));
    maxwell_free(s);

    // Immediate HSETP2 packs two separately signed half constants. .H_AND
    // combines the comparisons before writing the complementary predicate.
    w = 0x7e00000000070008ull | (2ull << 8) | (0xf0ull << 20) |
        (1ull << 29) | (0x100ull << 30) | (1ull << 56) |
        (4ull << 49) | (1ull << 53) | (7ull << 39);
    s = translate(w);
    assert(s->cbuf_mask == 0);
    assert(strstr(s->msl, "hx(0xc000bc00u, 0u)"));
    assert(strstr(s->msl, "hx_.x > hy_.x"));
    assert(strstr(s->msl, "l = (l && pp) && (h && pp); h = !l"));
    assert(strstr(s->msl, "p1 = l;") && strstr(s->msl, "p0 = h;"));
    maxwell_free(s);

    // Immediate HSET2 returns bit masks when BF is clear.
    w = 0x7c00000000070004ull | (2ull << 8) | (6ull << 49) | (7ull << 39);
    s = translate(w);
    assert(s->cbuf_mask == 0 && strstr(s->msl, "hx_.x >= hy_.x"));
    assert(strstr(s->msl, "r4 = (l ? 0xFFFFu"));
    maxwell_free(s);

    // Existing register forms keep their original comparison/BF field layout.
    w = 0x5d18000000070004ull | (2ull << 8) | (5ull << 20) |
        (2ull << 35) | (7ull << 39) | (1ull << 49);
    s = translate(w);
    assert(strstr(s->msl, "hx(r5, 0u)") && strstr(s->msl, "hx_.x == hy_.x"));
    assert(strstr(s->msl, "r4 = (l ? 0x3c00u"));
    maxwell_free(s);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-half-compare-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(source)
            subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"),
                            str(root / "test.c"), "-lm", "-o", str(root / "test")], check=True)
            shader = root / "dialog.frag"
            subprocess.run([str(root / "test"), str(shader)], check=True)
            validator = shutil.which("glslangValidator")
            if validator:
                subprocess.run([validator, "-V", str(shader), "-o", str(root / "dialog.spv")], check=True)


if __name__ == "__main__":
    unittest.main()
