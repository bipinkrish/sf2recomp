"""Extra fighters, live campaign gear, and a partially filled scrolling roster."""
import pathlib
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from test_settings import ROOT, compile_harness


HARNESS = r'''
#include <assert.h>
#include "hle/hooks.c"

static unsigned char campaign[0x200], copy[0x200], target[0x200], equipment[0x60];
static unsigned char weapon[0x280], replacement[0x280];
static bool campaign_exists = true, marked;
static unsigned clones, skins, attributes, perks, appended;
static unsigned portraits_created, portraits_destroyed;
static unsigned attached;
static float positions[3][2];
static uint64_t rects[] = {0x100, 0x200, 0x300};
static unsigned char document[0x20];

void guest_call(Cpu *c, uint64_t pc) {
    switch (pc) {
    case GUEST_STRING_ALLOC:
        c->x[0] = (uintptr_t)calloc(1, 0x16 + 2 * c->x[1]); return;
    case 0x70015ca0d0ull: {
        char name[64]; assert(dev_read_string(c->x[2], name, sizeof name));
        c->x[0] = !strcmp(name, "RecompSparring") ? marked : 0x1000;
        return;
    }
    case 0x70002e30a0ull: return;
    case 0x70015cb980ull: {
        unsigned n = LD32(c->x[1] + 0x10);
        fputs("XML ", stdout);
        for (unsigned i = 0; i < n; ++i) putchar(LD16(c->x[1] + 0x14 + i * 2));
        putchar('\n');
        c->x[0] = (uintptr_t)document; return;
    }
    case 0x70002d9050ull: c->x[0] = (uintptr_t)document; return;
    case 0x70015ca830ull: ++appended; return;
    case 0x7000d17e80ull: c->x[0] = campaign_exists ? (uintptr_t)campaign : 0; return;
    case 0x7000ea31d0ull:
        assert(c->x[0] == (uintptr_t)campaign);
        ++clones; memcpy(copy, campaign, sizeof copy); c->x[0] = (uintptr_t)copy; return;
    case 0x7000e9da70ull:
        assert(c->x[0] == (uintptr_t)copy); c->x[0] = (uintptr_t)equipment; return;
    case 0x7000e9e580ull: ++skins; return;
    case 0x7000e9ecd0ull: ++attributes; return;
    case 0x7000e9f7d0ull: ++perks; return;
    case 0x70015d8ec0ull:
        assert(c->x[0] == 0x1000 + attached && c->x[1] == 0x9000 && c->x[2] == 1);
        ++attached; return;
    case 0x700121e720ull: return;
    case 0x7001226480ull: ST8(c->x[0] + 0x10, c->x[1]); return;
    case 0x7000e73b90ull: ST32(c->x[0] + 0x1cc, c->x[1]); return;
    case 0x7000e71230ull:
    case 0x7000e72a50ull: ST64(c->x[0] + 0x180, c->x[1]); return;
    case 0x70015cf640ull: c->x[0] = 0x1234; return;
    case 0x70015cf720ull: {
        float rect[] = {100, 200, 320, 320};
        for (unsigned i = 0; i < 4; ++i) memcpy(&c->v[i], rect + i, 4);
        return;
    }
    case 0x70015cf3f0ull: {
        assert(c->x[0] == 0 && c->x[1] == 0x1234);
        float rect[6]; for (unsigned i = 0; i < 6; ++i) memcpy(rect + i, &c->v[i], 4);
        assert(rect[0] >= 100 && rect[1] >= 200);
        assert(rect[0] + rect[2] <= 420 && rect[1] + rect[3] <= 520);
        assert(rect[0] == 100 && rect[1] == 200 && rect[2] == 320 && rect[3] == 320);
        assert(rect[4] == 0.5f && rect[5] == 0.5f);
        c->x[0] = 0x6000 + ++portraits_created; return;
    }
    case 0x7001233760ull:
        assert(c->x[0] == 0 && c->x[1] >= 0x6001 && c->x[1] <= 0x6002);
        ++portraits_destroyed; return;
    case 0x700123a550ull:
        for (unsigned i = 0; i < 3; ++i) if (c->x[0] == rects[i]) {
            memcpy(positions[i], &c->v[0], 4);
            memcpy(positions[i] + 1, &c->v[1], 4); return;
        }
        assert(0);
    default: assert(0);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(g_settings.enable_shadow_in_sparring && g_settings.enable_titan_in_sparring);
    assert(settings_load_file(argv[1]));
    Cpu c = {.sp = 0x123456}; c.x[19] = 0xabcdef;
    Cpu before = c;
    sparring_extend_roster(&c, (uintptr_t)document);
    unsigned extras = g_settings.enable_shadow_in_sparring + g_settings.enable_titan_in_sparring;
    assert(appended == (extras ? extras * 2 + 1 : 0));
    marked = true;
    sparring_extend_roster(&c, (uintptr_t)document);
    assert(appended == (extras ? extras * 2 + 1 : 0)); // repeat parsing is idempotent
    assert(!memcmp(&c, &before, sizeof c));
    assert(sparring_roster_hook(&c, 0x7000fc8690ull) == 0 && c.x[22] == 33 + extras);

    ST64((uintptr_t)weapon + 0x10, guest_string(&c, "WEAPON_KATANA"));
    ST64((uintptr_t)replacement + 0x10, guest_string(&c, "Fists"));
    ST64((uintptr_t)campaign + 0xa8, (uintptr_t)weapon);
    for (unsigned off = 0xb0; off <= 0xc8; off += 8) ST64((uintptr_t)campaign + off, off + 0x1000);
    ST64((uintptr_t)target + 0x158, 0x11223344); // PvP attributes never come from the save
    unsigned char original[sizeof campaign]; memcpy(original, campaign, sizeof original);
    c = before; sparring_shadow_equipment(&c, (uintptr_t)target);
    assert(!memcmp(&c, &before, sizeof c) && !memcmp(original, campaign, sizeof original));
    for (unsigned off = 0xa0; off <= 0xc8; off += 8)
        assert(LD64((uintptr_t)target + off) == LD64((uintptr_t)campaign + off));
    assert(LD64((uintptr_t)target + 0x38) == (uintptr_t)equipment);
    assert(LD64((uintptr_t)target + 0x158) == 0x11223344);
    assert(clones == 1 && skins == 1 && attributes == 1 && perks == 1);
    ST64((uintptr_t)campaign + 0xa8, (uintptr_t)replacement);
    sparring_shadow_equipment(&c, (uintptr_t)target);
    assert(LD64((uintptr_t)target + 0xa8) == (uintptr_t)replacement); // fresh gear on next fight
    campaign_exists = false;
    sparring_shadow_equipment(&c, (uintptr_t)target);
    assert(clones == 2); // no campaign keeps the starter template

    unsigned char module[0xb0] = {0}, table[0x60] = {0}, array[0x20] = {0}, selector[0x80] = {0};
    ST64((uintptr_t)module + 0x20, (uintptr_t)table);
    ST64((uintptr_t)module + 0x98, (uintptr_t)array);
    ST64((uintptr_t)table + 0x18, (uintptr_t)selector);
    ST32((uintptr_t)module + 0x5c, 11); ST32((uintptr_t)module + 0x60, 4);
    for (unsigned count = 34; count <= 35; ++count) {
        ST64((uintptr_t)array + 0x18, count);
        const int moves[][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        for (unsigned index = 0; index < count; ++index) for (unsigned m = 0; m < 4; ++m) {
            ST32((uintptr_t)selector + 0x38, index % 11);
            ST32((uintptr_t)selector + 0x3c, index / 11);
            c = before; c.x[0] = (uintptr_t)module; c.x[1] = 0;
            c.x[2] = (uint32_t)moves[m][0]; c.x[3] = (uint32_t)moves[m][1];
            assert(sparring_roster_hook(&c, 0x7000e43da0ull) == -1);
            int x = ((int)(index % 11) + (int32_t)c.x[2] + 11) % 11;
            int y = ((int)(index / 11) + (int32_t)c.x[3] + 4) % 4;
            assert(y * 11 + x < count); // no controller path selects an empty cell
        }
    }
    sparring_view.table = (uintptr_t)table;
    sparring_view.content = rects[0];
    for (unsigned i = 0; i < 3; ++i) {
        positions[i][0] = i * 3;
        positions[i][1] = i * 5;
    }
    float height = 70; memcpy(table + 0x4c, &height, 4);
    ST32((uintptr_t)selector + 0x3c, 3);
    c = before; sparring_scroll_to_player(&c, 0);
    assert(!memcmp(&c, &before, sizeof c) && sparring_view.first_row == 1);
    assert(positions[0][0] == 0 && positions[0][1] == 70);
    // Native selector coroutines own these positions. Scrolling must leave
    // them alone, so highlight and portrait stay on the same logical cell.
    for (unsigned i = 1; i < 3; ++i) assert(positions[i][0] == i * 3 && positions[i][1] == i * 5);
    assert(!sparring_roster_cell_visible((uintptr_t)table, 0));
    assert(sparring_roster_cell_visible((uintptr_t)table, 34));
    ST32((uintptr_t)selector + 0x3c, 0); sparring_scroll_to_player(&c, 0);
    assert(sparring_view.first_row == 0 && !sparring_roster_cell_visible((uintptr_t)table, 34));
    for (unsigned i = 0; i < 3; ++i) assert(positions[i][1] == i * 5);
    MenuMouse mouse = {.y = 100, .pressed = 1, .held = 1};
    assert(!sparring_roster_scroll_input(&c, (uintptr_t)table, &mouse, true));
    mouse.pressed = 0; mouse.y = 140;
    assert(sparring_roster_scroll_input(&c, (uintptr_t)table, &mouse, true));
    assert(sparring_view.first_row == 1);
    mouse.held = 0; mouse.released = 1;
    assert(sparring_roster_scroll_input(&c, (uintptr_t)table, &mouse, true));
    assert(!sparring_view.pressed && !sparring_view.dragged); // suppress swipe release, allow a fresh tap
    mouse.released = 0; mouse.scroll_y = 1;
    assert(sparring_roster_scroll_input(&c, (uintptr_t)table, &mouse, true));
    assert(sparring_view.first_row == 0);
    mouse.scroll_y = -1;
    assert(!sparring_roster_scroll_input(&c, (uintptr_t)table, &mouse, false));
    assert(sparring_view.first_row == 0); // wheel outside the roster leaves it alone
    c = before;
    uint64_t portrait = sparring_portrait(&c, 0x9000, 0);
    assert(sparring_portrait(&c, 0x9000, 0) == portrait && portraits_created == 1);
    assert(sparring_portrait(&c, 0x9001, 1) != portrait && portraits_created == 2);
    assert(!memcmp(&c, &before, sizeof c));
    sparring_release_portraits(&c); sparring_release_portraits(&c);
    assert(portraits_destroyed == 2); // release only owned fitted sprites, never campaign textures
    // All seven independent UI transforms must scroll together. Reparenting
    // just the mask leaves the animated borders one row below the portrait.
    unsigned char other_selector[0x80] = {0}, moved[2][0x38] = {{0}};
    ST64((uintptr_t)table + 0x20, (uintptr_t)other_selector);
    ST64((uintptr_t)table + 0x28, 0x1000);
    ST64((uintptr_t)selector + 0x48, (uintptr_t)moved[0]);
    ST64((uintptr_t)other_selector + 0x48, (uintptr_t)moved[1]);
    for (unsigned player = 0; player < 2; ++player) {
        ST64((uintptr_t)moved[player] + 0x18, 3);
        for (unsigned i = 0; i < 3; ++i) ST64((uintptr_t)moved[player] + 0x20 + i * 8, 0x1001 + player * 3 + i);
    }
    c = before; sparring_attach_roster(&c, (uintptr_t)table, 0x9000);
    assert(attached == 7 && !memcmp(&c, &before, sizeof c));
    unsigned char panel[0x40] = {0}, title[0x200] = {0}, description[0x200] = {0}, hint[0x30] = {0};
    ST64((uintptr_t)panel + 0x18, (uintptr_t)hint);
    ST64((uintptr_t)panel + 0x20, (uintptr_t)title);
    ST64((uintptr_t)panel + 0x28, (uintptr_t)description);
    ST64((uintptr_t)target + 0x118, guest_string(&c, "BOSS_TITAN"));
    c = before; c.x[0] = (uintptr_t)panel; c.x[1] = (uintptr_t)target;
    Cpu vs_before = c;
    assert(sparring_roster_hook(&c, 0x7000e60ba0ull) == 1 && !memcmp(&c, &vs_before, sizeof c));
    assert(LD8((uintptr_t)panel + 0x10) && LD8((uintptr_t)description + 0x10));
    assert(LD32((uintptr_t)title + 0x1cc) == 1);
    assert(sparring_named(LD64((uintptr_t)title + 0x180), "MIND THROW"));
    assert(sparring_named(LD64((uintptr_t)description + 0x180), "SHIELD: AUTOMATIC"));
    assert(!LD8((uintptr_t)hint + 0x10)); // no fake Super hint for Titan
    ST64((uintptr_t)target + 0x118, guest_string(&c, "NAME_LYNX"));
    assert(sparring_roster_hook(&c, 0x7000e60ba0ull) == 0);
    assert(!LD32((uintptr_t)title + 0x1cc) && !LD32((uintptr_t)description + 0x1cc));
    reset_sparring_roster(); assert(!sparring_view.table);
}
'''


class SparringRosterTests(unittest.TestCase):
    def test_independent_settings_equipment_and_scroll_navigation(self):
        with tempfile.TemporaryDirectory(prefix="sf2-roster-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(HARNESS)
            compile_harness(root / "test.c", root / "test")
            for shadow, titan in ((True, True), (True, False), (False, True), (False, False)):
                config = root / "config.ini"
                config.write_text(f"[gameplay]\nenable_shadow_in_sparring={str(shadow).lower()}\n"
                                  f"enable_titan_in_sparring={str(titan).lower()}\n")
                result = subprocess.run([str(root / "test"), str(config)], capture_output=True, text=True, check=True)
                xml = [ET.fromstring(line[4:]) for line in result.stdout.splitlines() if line.startswith("XML ")]
                order = [n.get("Template") for n in xml if n.tag == "Char"]
                self.assertEqual(order, (["Pvp_Titan"] if titan else []) + (["Pvp_Shadow"] if shadow else []))
                characters = set(order)
                templates = {n.get("Name"): n for n in xml if n.tag == "Template"}
                self.assertEqual(set(templates), characters)
                if titan:
                    self.assertIsNone(templates["Pvp_Titan"].find("BossAbility"))
                    items = {i.get("Name") for i in templates["Pvp_Titan"].findall("./Items/Item")}
                    self.assertTrue({"WEAPON_TITAN_GIANT_SWORD", "RANGED_TITANS_HARPOON", "MAGIC_MIND_THROW"} <= items)
                    self.assertIsNotNone(templates["Pvp_Titan"].find("./Perks/Perk[@Name='PERK_TITAN']"))
                    self.assertEqual(templates["Pvp_Titan"].get("EnchantmentResistance"), "100")
                    expected = {
                        "PERK_TITANS_SHIELD": {"Cooldown": "180"},
                        "PERK_ITEM_SPECIAL_LIFESTEAL_WEAPON": {"Aspect": "1900", "Chance": "0.30"},
                        "PERK_ITEM_SPECIAL_LIFESTEAL_RANGED": {"Aspect": "1920", "Chance": "0.22"},
                        "PERK_ITEM_SPECIAL_ENFEEBLE_MAGIC": {"Aspect": "1920", "Chance": "0.50"},
                        "PERK_ITEM_SPECIAL_WEAKNESS_MAGIC": {"Aspect": "1920", "Chance": "0.50"},
                        "PERK_ITEM_SPECIAL_MAGIC_PAIN_RECHARGE_HELM": {"Aspect": "1920", "Chance": "0.70"},
                        "PERK_ITEM_SPECIAL_REGENERATION_HELM": {"Aspect": "1900", "ChanceFactor": "1.375"},
                        "PERK_ENLIGHTENMENT": {"Chance": "0.15", "Health": "0.15"},
                        "PERK_RICOCHET": {"Chance": "0.50"},
                    }
                    for name, values in expected.items():
                        self.assertEqual(templates["Pvp_Titan"].find(f"./Perks/Perk[@Name='{name}']/Set").attrib, values)

    def test_hook_instructions_match_supported_game(self):
        game = ROOT / "game/extracted/update/exefs/main"
        if not game.is_file():
            self.skipTest("local original game unavailable")
        from test_hook_groups import patches
        from nso import Nso
        nso = Nso(game)
        for site in (0x7000fc8430, 0x7000d11714, 0x7000fc8690, 0x7000e43da0,
                     0x7000d93fd0, 0x7000d94310, 0x7000e46a90, 0x7000e46de0,
                     0x7000d979e0, 0x7000e432a0, 0x7000e44080, 0x7000d96fb0, 0x7000e60ba0):
            self.assertEqual(nso.u32(site - 0x7000000000), patches.HOOKS[site][0], hex(site))
