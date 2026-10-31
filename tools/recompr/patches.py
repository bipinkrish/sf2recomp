"""SF2 v1.1.0 platform compatibility hooks.

The instruction word is checked during generation. Runtime hook results are:
0 = execute the instruction, 1 = handled, 2+ = jump to a listed local target.
No scene-loading, object-creation, binding-walker or scheduler bypasses live here.
"""

PATCHES = {}

HOOKS = {
    0x700011efb0: (0xf81c0ff7, (), "outline keyboard keys inside action buttons"),
    0x7000114f90: (0xf81e0ff3, (), "keep mapped labels in the replaced button theme"),
    0x7000ce0f40: (0xf81d0ff5, (), "mapped keyboard labels for graphic hints"),
    0x7000ce0dd0: (0xd65f03c0, (), "release destroyed hint state"),
    0x7000d9aec0: (0xaa1f03e1, (), "release destroyed image hint state"),
    0x7000cd7130: (0xd10103ff, (), "mapped labels for image-only button hints"),
    0x7001217700: (0xd100c3ff, (), "configured Unity target frame rate"),
    0x7000cdb690: (0xa9be4ff4, (), "mapped keyboard hint text"),
    0x7000ce0de0: (0xa9bc5ff8, (), "keyboard hint image visibility"),
    0x7000ce10a0: (0xf81d0ff5, (), "mapped keyboard hint label"),
    0x7000f9a060: (0xa9be4ff4, (), "keyboard toggle hint visibility"),
    0x7000844c30: (0x140c15b4, (), "corlib console output handle"),
    0x7000844c40: (0x140c15ac, (), "corlib console input handle"),
    0x7000844c50: (0x140c15a4, (), "corlib console error handle"),
    0x7000386660: (0x141f0f28, (), "console output handle, icall entry"),
    0x7000386650: (0x141f0f28, (), "console input handle, icall entry"),
    0x7000386670: (0x141f0f1c, (), "console error handle, icall entry"),
}

HOOKS_SDK = {}
