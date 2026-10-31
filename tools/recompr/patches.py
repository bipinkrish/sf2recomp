"""SF2 v1.1.0 platform compatibility hooks.

The instruction word is checked during generation. Runtime hook results are:
0 = execute the instruction, 1 = handled, 2+ = jump to a listed local target.
An optional fourth tuple field names a hook-group bit. Disabled groups run the
original instruction without calling the runtime hook.
No scene-loading, object-creation, binding-walker or scheduler bypasses live here.
"""

PATCHES = {}

HOOKS = {
    0x7000eabfe0: (0xf94013f5, (), "32:9 stage backdrop coverage after Render.Init"),
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
    0x7000e6b820: (0xa9bd57f6, (), "native desktop exit menu item"),
    0x7000e6ef20: (0xf81e0ff3, (), "exit to desktop from the game menu"),
    0x700011efb0: (0xf81c0ff7, (), "outline keyboard keys inside action buttons", "RT_HOOK_KEYBOARD_PROMPTS"),
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
