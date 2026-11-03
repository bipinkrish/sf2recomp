"""SF2 v1.1.0 platform compatibility hooks.

The instruction word is checked during generation. Runtime hook results are:
0 = execute the instruction, 1 = handled, 2+ = jump to a listed local target.
An optional fourth tuple field names a hook-group bit. Disabled groups run the
original instruction without calling the runtime hook.
No scene-loading, binding-walker or scheduler bypasses live here. Replay-only
hooks may drive native fight transitions under an explicit diagnostic policy.
"""

PATCHES = {}

# Replay's feature bit is 2u (runtime/replay.h). Keep the literal in generated
# C so adding diagnostic hooks does not change the shared CPU ABI header.
HOOKS = {
    0x7000f7cdc0: (0xa9be4ff4, (), "Android: keep the tutorial menu target on visible navigation"),
    # Campaign practice keeps completed content and skips save transactions.
    0x7000e7d4e0: (0xd101c3ff, (), "campaign: replay button on completed battle panel"),
    0x7000e7e7c0: (0xd10303ff, (), "campaign: start a practice fight"),
    0x7000d48820: (0xd101c3ff, (), "campaign: retry the selected practice fight"),
    0x7000d48870: (0xb40032f3, (0x7000d48c00,), "campaign: skip start progression and energy transaction"),
    0x7000d47398: (0xb9408288, (0x7000d47f60,), "campaign: show zero-reward results without completing the fight"),
    0x7000ca8a78: (0xf9407660, (0x7000ca8b20,), "campaign: skip practice end achievements"),
    0x7000c977a0: (0xd10143ff, (), "campaign: suppress practice achievement counters"),
    0x7000c99430: (0xd10203ff, (), "campaign: suppress practice counter completion"),
    0x7000c995f0: (0xd10203ff, (), "campaign: suppress practice achievement updates"),
    0x7000fc3fd0: (0x36000081, (), "campaign: suppress saves in practice mode"),
    0x7000fc32b0: (0xa9be4ff4, (), "campaign: suppress save backups in practice mode"),
    0x7000fc55c0: (0xd10103ff, (), "campaign: suppress direct settings saves in practice mode"),
    0x7000fdf620: (0xa9be4ff4, (), "campaign: suppress direct roster saves in practice mode"),
    0x7000f5d8c0: (0xa9be4ff4, (), "campaign: suppress startup settings saves in practice mode"),
    0x7000e8e750: (0xa9bb67fa, (), "campaign: leave practice on map initialization"),
    0x7000d9c7c0: (0xd101c3ff, (), "campaign: leave practice on dojo initialization"),
    0x7000e6c600: (0xd10143ff, (), "campaign: leave practice on main-menu initialization"),
    0x70015bc860: (0xa9be4ff4, (), "Android: complete managed movies when host playback ends"),
    0x70015bbf50: (0xfc1e0fe8, (), "Android: record only completed host movies as watched"),
    0x7001227dd0: (0xd100c3ff, (), "Android: primary touch held for legacy tutorial UI"),
    0x7001227e40: (0xd100c3ff, (), "Android: primary touch press for legacy tutorial UI"),
    0x7001227eb0: (0xd100c3ff, (), "Android: primary touch release for legacy tutorial UI"),
    0x7001227f20: (0xd100c3ff, (), "Android: primary touch position for legacy tutorial UI"),
    0x7000cdc110: (0xa9bd57f6, (), "hide menu footers while retaining dialog and combat hints"),
    0x7000c95bf0: (0xa9be4ff4, (), "Android: hide duplicate Switch combat controls"),
    0x7000c90a10: (0xf9402000, (), "Android: attach boss control after both HUD modules initialize"),
    0x7000c8f630: (0xa9be4ff4, (), "Android: reset mobile boss control on HUD initialization"),
    0x7000c90720: (0xa9be4ff4, (), "Android: release owned boss control with mobile HUD"),
    0x7000e1a610: (0xf81e0ff3, (), "Android: show the retained mobile controller"),
    0x7000c8f80c: (0x94165b1d, (), "Android: enable the mobile joystick container"),
    0x7000c8f830: (0xa8c567fa, (), "Android: enable the mobile action buttons"),
    0x7000c970b0: (0x2a1f03e0, (), "Android: route touch actions to the local player"),
    0x70012283e0: (0xf81e0ff3, (), "Android: actual Unity touch count"),
    0x7001228450: (0xd10203ff, (), "Android: Unity touch with stable finger IDs"),
    0x70012285c0: (0xf81e0ff3, (), "Android: enable Unity multitouch"),
    0x7000e6fca0: (0xa9be4ff4, (), "LAN main-menu item click"),
    0x7000e6fd00: (0xf9402800, (), "LAN main-menu item pointer down"),
    0x7000e6fd10: (0xf9402c00, (), "LAN main-menu item pointer up"),
    0x7000e6fd20: (0xf9403000, (), "LAN main-menu item hover"),
    0x7000e6fd30: (0xf9403400, (), "LAN main-menu item hover exit"),
    0x7000d985a0: (0x17fff810, (), "release Sparring mouse target on deactivation"),
    0x7000d985b0: (0xf81e0ff3, (), "release Sparring mouse target before free"),
    0x7000e5b660: (0xa9bc5ff8, (), "desktop keyboard pause-menu context"),
    0x7000e5b8f0: (0xa9be4ff4, (), "desktop keyboard combat context"),
    0x7000e5b9d0: (0xa9bd57f6, (), "desktop keyboard results context"),

    0x7000cf1360: (0xa9bd57f6, (), "LAN controls in the original Sparring tutorial", "4u"),

    # Native in-game LAN dialogs, selection and session lifecycle (bit 4).
    0x7000e6ed90: (0xf81d0ff5, (), "native Sparring LAN UI/session", "4u"),
    0x7000dc7550: (0xf9401801, (), "native Sparring LAN UI/session", "4u"),
    0x7000cee2d0: (0xa9bd57f6, (), "native Sparring LAN UI/session", "4u"),
    0x7000f48ef0: (0xd101c3ff, (), "native Sparring LAN UI/session"),
    0x7000d95000: (0xf81b0ff9, (), "native Sparring LAN UI/session", "4u"),
    0x7000d94fd0: (0xa9bf7bfd, (), "native Sparring LAN UI/session", "4u"),
    0x7000d982c0: (0xa9bf7bfd, (), "native Sparring LAN UI/session", "4u"),
    0x7000d98aa8: (0x7100041f, (), "LAN: wait for both session players to confirm", "4u"),
    0x7000d98b48: (0x7100081f, (), "LAN: show both session players' confirmations", "4u"),
    0x7000e44380: (0xf81b0ff9, (), "native Sparring LAN UI/session", "4u"),
    0x7000e46200: (0xf81d0ff5, (), "native Sparring LAN UI/session", "4u"),
    0x7000d98fa0: (0xa9be4ff4, (), "native Sparring LAN UI/session", "4u"),
    0x7000e43000: (0xa9bd57f6, (), "native Sparring LAN UI/session"),
    0x7000e45ea0: (0xa9be4ff4, (), "native Sparring LAN UI/session"),
    0x7000e51220: (0xd10143ff, (), "native Sparring LAN UI/session", "4u"),
    0x7000e51a20: (0xa9be4ff4, (), "native Sparring LAN UI/session", "4u"),
    0x7000e51760: (0xa9bd57f6, (), "native Sparring LAN UI/session", "4u"),
    0x7000e44aa0: (0xf81c0ff7, (), "native Sparring LAN UI/session", "4u"),
    0x7000e44df0: (0xf81c0ff7, (), "native Sparring LAN UI/session", "4u"),
    0x7000d98c30: (0xf81c0ff7, (), "native Sparring LAN UI/session", "4u"),
    0x7000d98de0: (0xf81c0ff7, (), "native Sparring LAN UI/session", "4u"),

    0x70000fd740: (0xaa1303e0, (), "latch desktop mouse before the current EventSystem updates"),
    0x70000fa8f0: (0xf81e0ff3, (), "enable existing Unity menu mouse handlers"),
    0x70000fa960: (0xa9be4ff4, (), "menu mouse button press"),
    0x70000fa9d0: (0xa9be4ff4, (), "menu mouse button release"),
    0x70000faa40: (0xa9be4ff4, (), "menu mouse held buttons"),
    0x70000faab0: (0xfc1d0fea, (), "menu mouse position in Unity screen coordinates"),
    0x70000fab70: (0xf81e0ff3, (), "menu mouse scroll delta"),
    0x70000fabe0: (0xf81e0ff3, (), "desktop menu uses mouse instead of Switch touch input"),
    0x70000fac50: (0xf81e0ff3, (), "inactive Switch touch slots cannot suppress menu mouse input"),
    0x7000eabfe0: (0xf94013f5, (), "Wide fight framing and 32:9 stage backdrop coverage after Render.Init"),
    0x7000ead4c0: (0x94061a6c, (), "32:9 foreground cover anchored to the fight floor during camera updates"),
    0x7000eabf0c: (0xbd008660, (), "PvP camera framing and combat margins use authored 16:9 reference"),
    0x7000ca7e80: (0xf81d0ff5, (), "replay: drive native pre-fight transitions by tick", "2u"),
    0x7000ca6df0: (0xf81e0ff3, (), "replay: drive native pre-fight transitions by tick", "2u"),

    0x7000c99e30: (0xd10303ff, (), "replay: scope combat construction", "2u"),
    0x7000c9dd50: (0xf81c0ff7, (), "replay: initialize native round and scope combat draw", "2u"),
    0x7000da027c: (0x39408268, (0x7000da01c8, 0x7000da005c), "LAN: keep native loading yield until both players loaded", "2u"),
    0x7000ca7c00: (0xa9be4ff4, (), "replay: scope active transition", "2u"),
    0x7001177d10: (0x29020401, (), "replay: controlled Nekki seed", "2u"),
    0x7001177d20: (0xb9401008, (), "replay: controlled Nekki LCG", "2u"),
    0x7001237b50: (0x6dbd23e9, (), "replay: controlled Unity float range", "2u"),
    0x7001237bc0: (0xf81d0ff5, (), "replay: controlled Unity integer range", "2u"),
    0x70010ed6a0: (0xa9bc5ff8, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1450: (0xa9bd57f6, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1550: (0xa9bd57f6, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1600: (0xfc1c0fe8, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1710: (0xfc1c0fe8, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f17d0: (0x6dbc23e9, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f18e0: (0x6dbc23e9, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f19a0: (0xf81c0ff7, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1ab0: (0xf81c0ff7, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1b80: (0xa9bc5ff8, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1c50: (0xf81c0ff7, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1d60: (0xa9bc5ff8, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1e80: (0x6dbc23e9, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f1f90: (0x6dbc23e9, (), "replay: isolate Nekki random call site", "2u"),
    0x70010f2de0: (0xf9400800, (), "replay: isolate Nekki random call site", "2u"),

    0x7000d961f0: (0xf81c0ff7, (), "shared LAN/replay: start synchronized PvP", "6u"),
    0x7000c9de18: (0xaa1303e0, (), "replay: before each unpaused combat substep", "2u"),
    0x7000c9de40: (0xb9402668, (), "replay: after combat and camera, before frame increment", "2u"),
    0x7000ff7af0: (0xf81d0ff5, (), "replay: logical fighter press", "2u"),
    0x7000ff7c40: (0x39456008, (), "replay: logical fighter release", "2u"),
    0x7001033e70: (0xd100c3ff, (), "32:9 stage backdrop scale during camera updates"),
    0x70015c8948: (0x1e212000, (), "Configured aspect UI without the original pillarbox borders"),
    0x70019016c0: (0xf81c0ff7, (), "Configured aspect graphics device resolution"),
    0x700190160c: (0xd63f02e0, (), "Configured aspect color surface descriptor"),
    0x700191177c: (0xd63f0100, (), "Configured aspect backbuffer storage sizing"),
    0x7001911810: (0xd63f0100, (), "Configured aspect depth storage sizing"),
    0x70019119cc: (0xd63f0100, (), "Configured aspect backbuffer creation"),
    0x70015fbb24: (0xb9006a69, (), "Configured aspect initial Switch screen width"),
    0x70015fbcf0: (0xd102c3ff, (), "Configured aspect Switch display modes"),
    0x70005c07c0: (0xf81d0ff5, (), "desktop console field uses SDL instead of Switch's modal keyboard"),
    0x7000f41cd0: (0xf81d0ff5, (), "desktop developer input on the guest update thread"),
    0x7000ceb3c0: (0xf81d0ff5, (), "desktop console input bridge"),
    0x7000ceb330: (0xa9be4ff4, (), "release desktop console capture"),
    0x7000ceb230: (0x320003e0, (), "configured developer console availability"),
    0x7000ce9d90: (0xa9be4ff4, (), "configured debug overlay visibility"),
    0x7000cec500: (0x17fff624, (), "configured FPS overlay"),
    0x7000cec850: (0x17fff550, (), "configured memory overlay"),
    0x7000ce9c10: (0xf81e0ff3, (), "configured build-info overlay"),
    0x7000cecaa0: (0xf81e0ff3, (), "configured model-info overlay"),
    0x7000da20a0: (0xd101c3ff, (), "desktop controls in the existing console prefab"),
    0x70000ad0e0: (0x2a1f03e0, (), "scoped local shop cheat checks"),
    0x700102ec00: (0xf81e0ff3, (), "configured original fight trainer"),
    0x7000e1a5b0: (0xf81e0ff3, (), "configured intro playback"),
    0x7000e1a760: (0xf81e0ff3, (), "configured fight AI"),
    0x7000e19adc: (0xd65f03c0, (), "apply internal gameplay options after original config parsing"),
    0x7000c89ea0: (0xa9bd57f6, (), "desktop trainer/config commands in original developer console"),
    0x7000fc5060: (0xf81d0ff5, (), "configured Extras video unlocks"),
    0x7000fc9230: (0xd101c3ff, (), "configurable save-independent sparring character unlocks"),
    0x7000fc8430: (0xf81d0ff5, (), "extend Sparring with Shadow and Titan"),
    0x7000d11714: (0xf0016648, (), "extend the separately loaded Sparring fighter templates"),
    0x7000fc8690: (0xaa1603e1, (), "allocate the enabled Sparring roster without empty cells"),
    0x7000e43da0: (0xf81c0ff7, (), "Sparring navigation skips absent final-row cells"),
    0x7000d93fd0: (0xd101c3ff, (), "Sparring roster keeps the original three-row portrait size"),
    0x7000d94310: (0xd10243ff, (), "Sparring roster scrolls to the selected row"),
    0x7000d979e0: (0xf81e0ff3, (), "rectangular Shadow/Titan roster portraits"),
    0x7000e46a90: (0xa9bd57f6, (), "fixed Shadow/Titan loadouts skip weapon selection"),
    0x7000e46de0: (0xa9bd57f6, (), "fixed Shadow/Titan loadouts skip weapon selection"),
    0x7000e432a0: (0xa9bd57f6, (), "refresh Shadow's campaign equipment in Sparring"),
    0x7000e44080: (0xa9bb67fa, (), "fixed Shadow/Titan Sparring weapons"),
    0x7000d96fb0: (0xa9bc5ff8, (), "isolated Shadow/Titan Sparring fighter parameters"),
    0x7000e60ba0: (0xd101c3ff, (), "hide Titan versus-screen ability panel"),
    0x7000e6b820: (0xa9bd57f6, (), "native desktop exit menu item"),
    0x7000e6ef20: (0xf81e0ff3, (), "exit to desktop from the game menu"),
    0x700011efb0: (0xf81c0ff7, (), "native UI circle meshes", "(RT_HOOK_KEYBOARD_PROMPTS | RT_HOOK_MOBILE_UI)"),
    0x7000114f90: (0xf81e0ff3, (), "keep mapped labels in the replaced button theme", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000ce0f40: (0xf81d0ff5, (), "mapped keyboard labels for graphic hints", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000ce0dd0: (0xd65f03c0, (), "release destroyed hint state", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000d9aec0: (0xaa1f03e1, (), "release destroyed image hint state", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000cd7130: (0xd10103ff, (), "mapped labels for image-only button hints", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7001217700: (0xd100c3ff, (), "configured Unity target frame rate"),
    0x7000cdb690: (0xa9be4ff4, (), "mapped keyboard hint text", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000ce0de0: (0xa9bc5ff8, (), "keyboard hint image visibility", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000ce10a0: (0xf81d0ff5, (), "mapped keyboard hint label", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000f9a060: (0xa9be4ff4, (), "keyboard toggle hint visibility", "RT_HOOK_KEYBOARD_PROMPTS"),
    0x7000844c30: (0x140c15b4, (), "corlib console output handle"),
    0x7000844c40: (0x140c15ac, (), "corlib console input handle"),
    0x7000844c50: (0x140c15a4, (), "corlib console error handle"),
    0x7000386660: (0x141f0f28, (), "console output handle, icall entry"),
    0x7000386650: (0x141f0f28, (), "console input handle, icall entry"),
    0x7000386670: (0x141f0f1c, (), "console error handle, icall entry"),
}

HOOKS_SDK = {}

# One checked list drives both generated call-site guards and runtime policy.
# These hooks stay unconditional: runtime opt-in can restore the original code,
# but disabling an optional UI feature must never remove the offline guard.
import re
from pathlib import Path

for line in (Path(__file__).resolve().parents[2] / "runtime/network_hooks.inc").read_text().splitlines():
    if not line or line.startswith("//"):
        continue
    match = re.fullmatch(r'NETWORK_HOOK\((0x[0-9a-f]+), (0x[0-9a-f]+), [A-Z]+, "([^"]+)"\)', line)
    if not match:
        raise ValueError(f"invalid network hook: {line}")
    site, word, description = match.groups()
    site, word = int(site, 16), int(word, 16)
    table = HOOKS if site < 0x7004000000 else HOOKS_SDK
    if site in table:
        raise ValueError(f"duplicate network hook: {site:#x}")
    table[site] = (word, (), "offline policy: " + description)
