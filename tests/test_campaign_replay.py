"""Completed boss and Gates practice leaves the roster and save unchanged."""
import pathlib
import subprocess
import tempfile
import unittest

from test_settings import ROOT, compile_harness


class CampaignReplayTests(unittest.TestCase):
    def test_button_start_results_exit_and_save_config(self):
        source = r'''
#include <assert.h>
#include <string.h>
#include "hle/hooks.c"

static unsigned char info[0xd0], battle[0xa8], fight[0x168], button[0x150], label[0x200];
static unsigned char content[0x48], video[0x40];
static unsigned char roster_battle[0x80], roster_fight[0x80];
static unsigned char old_battle[sizeof roster_battle], old_fight[sizeof roster_fight];
static unsigned status = 1, original_updates, enabled, shown, starts;
static unsigned replay_control, top_sibling;
static float movie_y;
static bool start_ok = true;
static char displayed[32];

void guest_call(Cpu *c, uint64_t pc) {
    switch (pc) {
    case GUEST_STRING_ALLOC: c->x[0] = (uintptr_t)calloc(1, 0x16 + 2*c->x[1]); return;
    case 0x7000e7d4e0ull:
        assert(campaign_replay_hook(c, pc) == 0); // original runs once, no recursion
        ++original_updates; return;
    case 0x7000efe380ull: c->x[0] = status; return;
    case 0x7000e7e000ull: assert(c->x[0] == (uintptr_t)info); c->x[0] = (uintptr_t)battle; return;
    case 0x7000d448f0ull: assert(c->x[1] == (uintptr_t)battle); c->x[0] = (uintptr_t)fight; return;
    case 0x7000e73cd0ull: c->x[0] = 105; return;
    case 0x7000e72180ull: assert(c->x[1] == 90 || c->x[1] == 105); return;
    case 0x7000e73b90ull: assert(c->x[0] == (uintptr_t)label && c->x[1] == 1); return;
    case 0x70011cbb80ull: assert(dev_read_string(c->x[1], displayed, sizeof displayed)); return;
    case 0x70011cb2f0ull: replay_control = c->x[1]; return;
    case 0x700121e6b0ull: c->x[0] = c->x[0] == (uintptr_t)video ? 0x5678 : 0x4321; return;
    case 0x70015d9ee0ull: assert(c->x[0] == 0x4321); ++top_sibling; return;
    case 0x700123a410ull:
    case 0x700123a6d0ull: {
        assert(c->x[0] == 0x5678);
        float x = pc == 0x700123a410ull ? 0 : 300;
        float y = pc == 0x700123a410ull ? -400 : 300;
        memcpy(&c->v[0], &x, 4); memcpy(&c->v[1], &y, 4); return;
    }
    case 0x700123a550ull:
        assert(c->x[0] == 0x5678); memcpy(&movie_y, &c->v[1], 4); return;
    case 0x70005d8310ull: enabled = c->x[1]; return;
    case 0x700121e720ull: c->x[0] = 0x1234; return;
    case 0x7001226480ull: assert(c->x[0] == 0x1234); shown = c->x[1]; return;
    case 0x7000d48820ull: {
        ++starts;
        assert(campaign_replay_fight == (uintptr_t)fight);
        assert(c->x[1] == (uintptr_t)fight && c->x[3] == (uintptr_t)battle);
        assert(c->x[4] == 1 && c->x[5] == 0); // gong, no pre-fight quest comparison
        c->x[19] = c->x[1];
        assert(campaign_replay_hook(c, 0x7000d48870ull) == 2);
        c->x[0] = start_ok;
        return;
    }
    default: assert(0);
    }
}

static void unchanged(void) {
    assert(!memcmp(roster_battle, old_battle, sizeof old_battle));
    assert(!memcmp(roster_fight, old_fight, sizeof old_fight));
}
int main(int argc, char **argv) {
    assert(argc == 4 && !g_settings.use_completed_save);
    ST64((uintptr_t)info + 0x40, (uintptr_t)button);
    ST64((uintptr_t)info + 0x88, (uintptr_t)content);
    ST64((uintptr_t)content + 0x30, (uintptr_t)video);
    ST64((uintptr_t)button + 0x120, (uintptr_t)label);
    ST64((uintptr_t)battle + 0x10, (uintptr_t)roster_battle);
    ST64((uintptr_t)fight + 0x20, (uintptr_t)roster_fight);
    ST64((uintptr_t)fight + 0x40, (uintptr_t)battle);
    ST16((uintptr_t)battle + 0xa0, 6);
    memset(roster_battle, 0x5a, sizeof roster_battle);
    memset(roster_fight, 0xa5, sizeof roster_fight);
    memcpy(old_battle, roster_battle, sizeof old_battle);
    memcpy(old_fight, roster_fight, sizeof old_fight);
    Cpu c = {0}; c.x[0] = (uintptr_t)info; c.x[1] = (uintptr_t)battle;
    c.x[19] = 0xabcdef; c.sp = 0x123456;
    Cpu before = c;
    // Original bosses have bodyguards; after the Gates the six maps instead
    // select single-boss intermission battles. Include both Gates variants.
    const unsigned types[] = {2, 2, 2, 2, 2, 2, 22, 8, 8, 8, 8, 8, 8, 11, 19, 20};
    for (unsigned boss = 0; boss < sizeof types / sizeof *types; ++boss) {
        ST32((uintptr_t)battle + 0x20, types[boss]);
        ST16((uintptr_t)battle + 0xa0, types[boss] == 8 || types[boss] == 19 ? 1 : 6);
        ST32((uintptr_t)fight + 0x50, LD16((uintptr_t)battle + 0xa0) - 1);
        enabled = shown = 0;
        assert(campaign_replay_hook(&c, 0x7000e7d4e0ull) == 1);
        assert(enabled && shown && !strcmp(displayed, "REFIGHT"));
        assert(replay_control == 2 && top_sibling == boss + 1);
        assert(movie_y == -300); // original position plus a third of the movie height
        assert(!memcmp(&c, &before, sizeof c));
        assert(campaign_replay_hook(&c, 0x7000e7e7c0ull) == 1);
        assert(!memcmp(&c, &before, sizeof c));
        assert(campaign_replay_hook(&c, 0x7000fc3fd0ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000fc32b0ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000fc55c0ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000fdf620ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000f5d8c0ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000c977a0ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000c99430ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000c995f0ull) == 1);
        assert(campaign_replay_hook(&c, 0x7000ca8a78ull) == 2);
        unsigned char result[0x80] = {0};
        unsigned char stack[0x40] = {0}, combo[0x60] = {0}, prize[0x70];
        memset(prize, 0x5a, sizeof prize);
        ST64((uintptr_t)combo + 0x40, (uintptr_t)prize);
        ST64((uintptr_t)stack + 0x18, (uintptr_t)combo);
        c.sp = (uintptr_t)stack;
        c.x[19] = (uintptr_t)result; c.x[20] = (uintptr_t)fight;
        for (unsigned outcome = 0; outcome < 3; ++outcome) {
            ST32((uintptr_t)result + 0x10, outcome); // win, loss, quit
            assert(campaign_replay_hook(&c, 0x7000d47398ull) == 2);
            assert(LD64((uintptr_t)result + 0x38) == (uintptr_t)combo);
            assert(LD64((uintptr_t)prize + 0x10) == 0);
            assert(LD64((uintptr_t)prize + 0x68) == 0);
            unchanged();
        }
        // Destination callbacks release practice protection before normal play.
        assert(campaign_replay_hook(&c, 0x7000e8e750ull) == -1);
        assert(!campaign_replay_fight);
        assert(campaign_replay_hook(&c, 0x7000fc3fd0ull) == 0);
        c = before;
    }
    assert(starts == 16 && original_updates == 16);
    campaign_show_replay(&c, (uintptr_t)info, (uintptr_t)battle);
    assert(movie_y == -300); // refreshing the same selection must not move it again
    for (unsigned i = 0; i < sizeof types / sizeof *types; ++i) {
        ST32((uintptr_t)battle + 0x20, types[i]);
        for (status = 2; status <= 3; ++status)
            assert(!campaign_can_replay(&c, (uintptr_t)battle));
    }
    status = 2; // undefeated bosses preserve the normal fight path
    campaign_show_replay(&c, (uintptr_t)info, (uintptr_t)battle);
    assert(replay_control == 0 && movie_y == -400); // restore normal layout and A
    assert(!campaign_can_replay(&c, (uintptr_t)battle));
    status = 3;
    assert(!campaign_can_replay(&c, (uintptr_t)battle));
    status = 1;
    ST32((uintptr_t)battle + 0x20, 3); // completed tournaments keep original UI by default
    assert(!campaign_can_replay(&c, (uintptr_t)battle));
    ST32((uintptr_t)battle + 0x20, 2);
    start_ok = false;
    assert(campaign_replay_hook(&c, 0x7000e7e7c0ull) == 1 && !campaign_replay_fight);
    unchanged();

    assert(settings_load_file(argv[1]) && g_settings.use_completed_save);
    assert(campaign_replay_hook(&c, 0x7000fc3fd0ull) == 0); // completed profile saves normally outside Refight
    assert(settings_load_file(argv[2]) && !g_settings.use_completed_save);
    assert(settings_load_file(argv[3]) && !g_settings.use_completed_save);
    reset_campaign_replay(); unchanged();
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-campaign-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(source)
            configs = []
            for name, value in (("on", "true"), ("off", "false"), ("invalid", "maybe")):
                path = root / f"{name}.ini"
                path.write_text(f"[storage]\nuse_completed_save = {value}\n")
                configs.append(str(path))
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test"), *configs], check=True)

    def test_hooks_and_jump_targets_match_supported_game(self):
        game = ROOT / "game/extracted/update/exefs/main"
        if not game.is_file():
            self.skipTest("local original game unavailable")
        from test_hook_groups import patches
        from nso import Nso
        module = Nso(game)
        hooks = {site: hook for site, hook in patches.HOOKS.items() if hook[2].startswith("campaign:")}
        self.assertEqual(len(hooks), 17)
        for site, hook in hooks.items():
            self.assertEqual(module.u32(site - 0x7000000000), hook[0], hex(site))
        self.assertEqual(hooks[0x7000d48870][1], (0x7000d48c00,))
        self.assertEqual(hooks[0x7000d47398][1], (0x7000d47f60,))
        self.assertEqual(hooks[0x7000ca8a78][1], (0x7000ca8b20,))

    def test_supplied_setting_is_off(self):
        import configparser
        config = configparser.ConfigParser()
        config.read(ROOT / "sf2recomp.ini")
        self.assertFalse(config.getboolean("storage", "use_completed_save"))

    def test_bundled_snapshot_is_complete_and_matches_manifest(self):
        import hashlib
        import json
        import xml.etree.ElementTree as ET
        bundle = ROOT / "profiles/completed"
        manifest = json.loads((bundle / "manifest.json").read_text())
        for name, expected in manifest["files"].items():
            self.assertEqual(hashlib.sha256((bundle / name).read_bytes()).hexdigest(), expected)
        root = ET.fromstring((bundle / "save_0/userdata/users.xml").read_text())
        for zone, boss in enumerate(("BOSS_LYNX", "BOSS_HERMIT", "BOSS_BUTCHER", "BOSS_WASP",
                                    "BOSS_HUNTRESS", "BOSS_SAMURAI", "C3_BOSS_TITAN"), 1):
            prefix = f"ZONE_{zone}|{boss}|"
            fights = [fight for fight in root.iter("Fight") if fight.get("IDS", "").startswith(prefix)]
            self.assertEqual(len(fights), 6, boss)
            self.assertTrue(all(int(fight.get("CompletedCount", "0")) >= 1 for fight in fights), boss)

        for zone, boss in enumerate(("LYNX", "HERMIT", "BUTCHER", "WASP", "HUNTRESS", "SAMURAI"), 1):
            prefix = f"ZONE_{zone}|BOSS_{boss}_INTERMISSION|"
            fights = [fight for fight in root.iter("Fight") if fight.get("IDS", "").startswith(prefix)]
            self.assertEqual(len(fights), 1, prefix)
            self.assertGreaterEqual(int(fights[0].get("CompletedCount", "0")), 1, prefix)
        gates = [fight for fight in root.iter("Fight") if fight.get("IDS") == "ZONE_6|FINAL_BATTLE|1"]
        self.assertEqual(len(gates), 1)
        self.assertGreaterEqual(int(gates[0].get("CompletedCount", "0")), 1)
