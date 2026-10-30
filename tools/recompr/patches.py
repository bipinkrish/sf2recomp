"""SF2 v1.1.0 platform compatibility hooks.

The instruction word is checked during generation. Runtime hook results are:
0 = execute the instruction, 1 = handled, 2+ = jump to a listed local target.
No scene-loading, object-creation, binding-walker or scheduler bypasses live here.
"""

PATCHES = {}

HOOKS = {
    0x7000844c30: (0x140c15b4, (), "corlib console output handle"),
    0x7000844c40: (0x140c15ac, (), "corlib console input handle"),
    0x7000844c50: (0x140c15a4, (), "corlib console error handle"),
    0x7000386660: (0x141f0f28, (), "console output handle, icall entry"),
    0x7000386650: (0x141f0f28, (), "console input handle, icall entry"),
    0x7000386670: (0x141f0f1c, (), "console error handle, icall entry"),
}

HOOKS_SDK = {}
