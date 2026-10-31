"""The original localized Exit item participates in the native menu and quits."""
import pathlib
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


class QuitMenuTests(unittest.TestCase):
    def test_reveal_and_quit_preserve_guest_state(self):
        source = r'''
#include <assert.h>
#include "hle/hooks.c"
#include "ui/menu_stubs.c"

typedef struct Node {
    unsigned char name[128];
    struct Node *children[4];
    int count;
    bool active;
} Node;
static Node main_section, settings, content, story, exit_item, extra;
static Node *current_root;
static int activations;

static void named(Node *node, const char *name) {
    ST32((uintptr_t)node->name + 0x10, strlen(name));
    for (size_t i = 0; i < strlen(name); ++i)
        ST16((uintptr_t)node->name + 0x14 + i * 2, name[i]);
}

void guest_call(Cpu *c, uint64_t pc) {
    Node *node = (Node *)(uintptr_t)c->x[0];
    switch (pc) {
    case 0x700121e6b0ull: c->x[0] = (uintptr_t)current_root; break;
    case 0x700122e460ull: c->x[0] = (uintptr_t)node->name; break;
    case 0x70015d9e00ull: c->x[0] = node->count; break;
    case 0x70015da1f0ull:
        assert(c->x[1] < node->count);
        c->x[0] = (uintptr_t)node->children[c->x[1]]; break;
    case 0x700121e720ull: break;  // transform -> GameObject
    case 0x7001226480ull:
        assert(node == &exit_item && c->x[1] == 1);
        node->active = true;
        ++activations;
        break;
    default: assert(0);
    }
    c->x[19] = 0xdeadbeef;  // nested guest calls must preserve the caller
}
void *hle_alloc(size_t size) { return calloc(1, size); }
bool rt_probe_u64(uint64_t a, uint64_t *v) { return false; }
bool input_any_gamepad_connected(void) { return false; }
bool nvn_texture_region_color(const NvnTexture *t, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, float rgb[3]) { return false; }

int main(void) {
    g_settings.developer_console = g_settings.developer_overlays = false;
    named(&main_section, "MainSection"); named(&settings, "SettingsSection");
    named(&content, "ListContent"); named(&story, "MenuItemButton_story");
    named(&extra, "MenuItemButton_extra"); named(&exit_item, "MenuItemButton_exit");
    main_section.children[0] = settings.children[0] = &content;
    main_section.count = settings.count = 1;
    content.children[0] = &story; content.children[1] = &extra;
    content.children[2] = &exit_item; content.count = 3;
    story.active = true;
    Cpu cpu, saved;
    memset(&cpu, 0xa5, sizeof cpu); saved = cpu;
    current_root = &settings;
    assert(rt_hook(&cpu, 0x7000e6b820ull) == 0);
    assert(!exit_item.active && activations == 0);
    assert(!memcmp(&cpu, &saved, sizeof cpu));
    current_root = &main_section;
    assert(rt_hook(&cpu, 0x7000e6b820ull) == 0);
    assert(exit_item.active && activations == 1);
    assert(story.active && !extra.active && content.count == 3);
    assert(!memcmp(&cpu, &saved, sizeof cpu));
    assert(!menu_quit_requested());
    assert(rt_hook(&cpu, 0x7000e6ef20ull) == 1);
    assert(menu_quit_requested());
    assert(!memcmp(&cpu, &saved, sizeof cpu));
    menu_request_quit();
    assert(menu_quit_requested());  // duplicate requests remain harmless
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-quit-menu-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(source)
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
