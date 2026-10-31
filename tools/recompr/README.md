# SF2 static recompiler

Forked from [LAS Recomp](https://github.com/Peiikai/LASRecomp)'s generator
(MIT; see ../../licenses/LASRecomp.txt). It translates AArch64 NSO instructions into C functions
`void f_<guestaddr>(Cpu *c)` and emits address tables for the runtime dispatcher.

| File | Purpose |
|---|---|
| `nso.py` | NSO decompression, segments, dynamic symbols and relocations |
| `analyze.py` | Function discovery using `.eh_frame_hdr` and instruction analysis |
| `lifter.py`, `simd_c.py` | Instruction and portable FP/SIMD lowering |
| `gen.py` | C generation; `--symbols` seeds IL2CPP function boundaries and names |
| `game_addresses.py` | Supported SF2 build IDs and guest address definitions |
| `patches.py` | Console-stream, Unity frame-rate, and runtime keyboard-hint hooks; no instruction patches or SDK hooks |

Hook metadata is `(instruction, jump_targets, description[, feature_bit])`.
Optional groups are configured before guest threads start. A disabled site's
generated guard skips the callback and executes its checked original instruction.
Keyboard hints use `RT_HOOK_KEYBOARD_PROMPTS`; console and frame-rate compatibility
hooks stay unconditional. Hint bookkeeping is allocated on first enabled use and
freed on guest reset. This is a runtime gate, so executable code and one flag check
per optional site remain even when the feature is off.

From the repository root, with your extracted update and IL2CPP symbol map:

```sh
python3 tools/recompr/gen.py main game/extracted/update/exefs/main \
  0x7000000000 game/gen/main --symbols game/il2cpp/sym_main.txt
python3 tools/recompr/gen.py sdk game/extracted/update/exefs/sdk \
  0x7004000000 game/gen/sdk
make -j8
```

Regenerate after editing `patches.py`; generated C and game inputs are ignored by
git. The runtime supplies `cpu.h`, dispatch, loading/relocation, system services
and Vulkan rendering. Main and SDK are translated; the runtime loader replaces
`rtld`, and movie imports from `subsdk0` use the host decoder in
`runtime/hle/movie.c`. This does not translate the entire movie module; track
operations and broader cinematic behavior remain limited.

Only update v131072 is accepted. Generated tables embed all 32 main/SDK NSO build-ID
bytes; the runtime refuses mismatched files before starting a graphics device.
Linux reaches the original menu and Story dialogue. The generator reads LSDA
call-site catch metadata and emits protected-call tables; the runtime can resume
matching typed or catch-all landing pads. Cleanup and rethrow paths remain
unsupported and broader exception coverage is still needed. See the root README
for launch options and current platform limitations.
