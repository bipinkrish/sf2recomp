"""Hide authored menu footers without touching dialogs or the combat hint lists."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_settings import compile_harness


class FooterHintTests(unittest.TestCase):
    def test_only_footer_modules_lose_graphics_and_pointer_targets(self):
        source = r'''
#include <assert.h>
#include "hle/hooks.c"
static unsigned char string_data[256];
static const char *object_name;
static bool hidden, blocks, existing;
static unsigned added;
static void name_string(void) {
    ST32((uintptr_t)string_data + 0x10, strlen(object_name));
    for (unsigned i = 0; i < strlen(object_name); ++i)
        ST16((uintptr_t)string_data + 0x14 + i * 2, object_name[i]);
}
void guest_call(Cpu *c, uint64_t pc) {
    switch (pc) {
    case 0x700122e460ull: name_string(); c->x[0] = (uintptr_t)string_data; break;
    case GUEST_STRING_ALLOC: c->x[0] = (uintptr_t)string_data; break;
    case 0x70008ea5c0ull: c->x[0] = 123; break;
    case 0x700121e720ull: c->x[0] = 456; break;
    case 0x700121e790ull: assert(c->x[1] == 123); c->x[0] = existing ? 789 : 0; break;
    case 0x70012260e0ull: assert(c->x[0] == 456 && c->x[1] == 123); ++added; c->x[0] = 789; break;
    case 0x7000af7fb0ull: {
        float alpha; memcpy(&alpha, &c->v[0], sizeof alpha);
        assert(c->x[0] == 789 && alpha == 0); hidden = true; break;
    }
    case 0x7000af8110ull: assert(c->x[0] == 789); blocks = c->x[1]; break;
    default: assert(!"unexpected guest call");
    }
}
int main(void) {
    Cpu cpu = {0}; cpu.x[0] = 100; cpu.x[19] = 200; cpu.v[0] = 12345;
    Cpu saved = cpu;
    const char *footers[] = {"GeneralMenuHintList", "MapHintList", "ProfileHintList", "ShopHintList",
                            "DojoHintList", "PvPLeftHintList", "PvPRightHintList"};
    for (unsigned i = 0; i < sizeof footers / sizeof footers[0]; ++i) {
        object_name = footers[i]; hidden = false; blocks = true;
        assert(platform_ui_hook(&cpu, 0x7000cdc110ull) == 0);
        assert(hidden && !blocks && !memcmp(&cpu, &saved, sizeof cpu));
    }
    assert(added == 7); existing = true; object_name = footers[0];
    hide_menu_footer(&cpu, 100); assert(added == 7); // no duplicate CanvasGroups
    const char *preserved[] = {"FightLeftHintList", "FightRightHintList", "HintUI", "Dialog",
                              "GeneralMenuHintListDialog", ""};
    for (unsigned i = 0; i < sizeof preserved / sizeof preserved[0]; ++i) {
        object_name = preserved[i]; hidden = false; blocks = true;
        assert(platform_ui_hook(&cpu, 0x7000cdc110ull) == 0);
        assert(!hidden && blocks && !memcmp(&cpu, &saved, sizeof cpu));
    }
    reset_platform_ui(); assert(!footer_canvas_group_type);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-footer-") as tmp:
            root = Path(tmp)
            (root / "footer.c").write_text(source)
            compile_harness(root / "footer.c", root / "footer")
            subprocess.run([str(root / "footer")], check=True)
