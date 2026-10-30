# Portability and a full-game release gate

Audit date: 2026-10-30. This describes the current source tree and remaining work,
not implemented Windows/macOS support or a completed game.

The fastest route to a complete first release is **Linux x86-64 first**, then
Windows x86-64 using the same Vulkan renderer. macOS needs both host adaptation
and a tested graphics portability path. Recompiled game logic, assets, and the
remaining guest exception/video work are shared across these targets.

## What is established

Linux x86-64 has visually verified menu and Story/Sensei dialogue captures using
software Vulkan. Control remapping, runtime INI loading, source-build identity
checks, and portable file placement have regression tests. These establish a
working development port, not playable combat or campaign completion. Hardware
Vulkan, combat, full progression, and Windows/macOS have not been established by
those captures or tests.

The public deliverable is the compiler/runtime source and licensed dependencies.
A user runs extraction, generation, native compilation, and packaging locally;
the resulting executable embeds translated game code and uses their extracted
assets. `tools/convert.py` currently implements this workflow for Linux only.

## Remaining work by target

| Area | Windows x86-64 | macOS Intel | macOS Apple Silicon |
|---|---|---|---|
| Host OS implementation | Add `platform/windows.c` and the missing compatibility/thread headers, or replace their consumers with a consistent host abstraction. Implement guest address reservation/commit/protection, clocks, thread/process control, executable discovery, and crash reporting. | Add a Darwin host implementation; Linux `/proc`, userfaultfd, affinity, and signal-context layouts do not transfer directly. | Same Darwin work, plus ARM64 signal-context handling and host-page-size validation. |
| C/C++ toolchain | Start with Clang/MinGW or another explicitly tested Clang-based environment. Current GNU C extensions, 128-bit types/atomics, and mandatory tail calls are part of the implementation contract; do not assume MSVC can compile it unchanged. | Add a Mach-O build/link profile and native dependencies. Remove Linux ELF linker flags and verify 128-bit atomics. | Remove x86-only `-mcx16`; select ARM64 code generation deliberately. Test portable C SIMD first or validate the native ARM64-assembly lift against the portable version. |
| Rendering | Keep Vulkan, use Windows SDL surfaces/loader and test guest-memory import/fallbacks on supported drivers. Linux DMA-BUF paths remain Linux-only. | Try the existing Vulkan backend through MoltenVK with complete feature/format negotiation; otherwise make an explicit Metal-backend decision. | Same MoltenVK path, with Apple-GPU limits, resource synchronization, and shader behavior tested on actual hardware. |
| Files/config | Replace Linux-only file shims; handle drive-letter, separator, Unicode and long-path cases. Resolve runtime INI beside the actual executable and keep saves writable. | Preserve portable-folder semantics for a command-line folder; define an explicit writable save/config location before adding an application bundle. | Same; do not silently write inside a sealed/read-only application bundle. |
| User-side conversion | Port dependency setup/build and packaging: tools currently assume a POSIX shell, GNU utilities, Linux library names and loader inspection. MSYS2 can be a supported build environment, but the resulting game must run without a Linux VM. | Port shell utilities, tool builds, dependency discovery and `.dylib` packaging/install names. | Same, with ARM64 dependencies and target-aware generation; an x86-generated executable is not a native ARM64 deliverable. |
| Validation | Run the same full-game gate on Windows; successful Linux gameplay is necessary shared evidence, not Windows validation. | Validate Intel macOS separately from Apple Silicon. | Validate native ARM64 separately; Rosetta execution does not establish it. |

The matrix is a work breakdown, not an estimate that every item requires a new
subsystem. SDL already provides portable window, controller, and audio APIs.
The missing work is the runtime's own host integration and verification, not a
rewrite of the original game in a new engine.

## Concrete source blockers

Line numbers are audit anchors and will move as implementation changes.

- `Makefile:19-26,33`: unconditionally applies x86 `-mcx16`, ELF/Linux linker
  flags, and `runtime/platform/linux.c`. OS and CPU selection need separate
  explicit build profiles.
- `runtime/platform.h:11-14` and `runtime/kernel.c:11-12`: Windows branches refer
  to missing `platform/win_compat.h` and `platform/windows_threads.h`.
- `runtime/rt.h:29-47`, `runtime/cpu.h:59-77`, and `runtime/main.c:38-59`:
  guest addresses are dereferenced as host pointers. The entire
  `[0x7000000000, 0x7320000000)` window (12.5 GiB of virtual address space, not
  necessarily resident RAM) must be safely available at those exact addresses.
  The replacement platform layer must reserve without overwriting existing
  mappings, commit/reset zeroed pages correctly, and handle reservation failure.
- `runtime/platform/linux.c:65-94,114-132,292-310,421-438`: Linux write tracking,
  `mmap` flags, `/proc` executable/command-line discovery, and Linux signal context
  need host-specific equivalents. Write tracking can initially use the existing
  conservative fallback; it must not pretend untracked guest writes are absent.
- `runtime/kernel.c:989-999`: the stack-spy handler uses Linux x86 `gregs` fields
  under an all-non-Windows guard. This also prevents treating Linux ARM64 as
  already supported merely because the generator understands ARM64.
- `runtime/dispatch.c:219-225`: safe probe reads open `/proc/self/mem`. Move
  this behind a host API rather than replacing it with unsafe dereferences.
- `runtime/paths.c:16-41`: current absolute-path/separator and recursive-directory
  logic follows POSIX rules. Add Windows path semantics without losing the
  runtime INI/default-data policy covered by `tests/test_paths.py`.
- `runtime/cpu.h:105-119`, `runtime/simd.h:15-24,212-230`, and
  `tools/recompr/gen.py:550-551`: 128-bit atomics/vectors, host FP rounding and
  the generator's host-based SIMD default need target-specific compile and
  semantic tests. An ARM64 host does not eliminate Switch OS/GPU replacement.
- `runtime/hle.h:15-20` and `runtime/dispatch.c:30-38`: Mach-O and PE registration
  branches exist, but that is partial scaffolding, not proof of a complete port.
- `tools/setup_runtime.sh` and `tools/convert.py`: setup/package logic is Linux
  specific, including GNU utility use and `.so` dependency packaging. Make the
  target explicit and refuse unsupported targets until their build is real.

Microsoft documents separate address reservation and commit behavior through
[VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc).
That provides a host primitive; it does not automatically reproduce the guest
mapping semantics. Clang's [musttail contract](https://clang.llvm.org/docs/AttributeReference.html#musttail)
also requires matching call signatures/calling conventions. Preserve the
translated dispatch tail-call guarantee when changing compiler or ABI.

## macOS graphics decision

A native Metal backend is **not automatically required**. The first experiment
should be the existing Vulkan renderer with a pinned MoltenVK version and a
capability report from each target GPU. MoltenVK translates Vulkan/SPIR-V to
Metal/MSL; its current upstream documentation describes a Vulkan 1.4 subset,
including Apple Silicon support. See the official
[MoltenVK introduction](https://github.com/KhronosGroup/MoltenVK/blob/main/README.md).

The current backend is not ready to be pointed at that library unchanged:

1. `device.c:142-154` lacks Vulkan portability enumeration, and its device
   extension list at `313-355` does not enable the portability-subset extension.
   The loader requires the portability enumeration flag to expose MoltenVK
   devices; a device advertising the subset extension must have it enabled.
   See [MoltenVK integration](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
   and the [Khronos subset contract](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_subset.html).
2. `device.c:461-471` requests Vulkan 1.1/1.2/1.3 features largely unconditionally.
   Query each actual feature and limit, implement a correct fallback or reject
   an unsupported configuration with a precise message. Version number alone
   is insufficient.
3. `maxwell/translate.c:1813-1819` requires integer and subgroup shader extensions;
   lazy global-memory shaders also use buffer references at `1907`. Check their
   generated SPIR-V and required capabilities on each supported Metal GPU.
   `shaderInt64` is currently only logged as a warning when missing in
   `device.c:507`; a shader that needs it cannot be counted correct anyway.
4. Query portability-subset limits for the actual operations used, including
   resource formats/swizzles, vertex attribute layout, primitive restart and
   stencil behavior. Do not assume a desktop Vulkan feature remains identical
   through another graphics API.
5. Validate CPU/GPU shared guest-memory access and synchronization. Do not
   categorically assume host-memory import or buffer-device addresses are absent:
   the current MoltenVK guide lists both; buffer-device-address support has a
   GPU argument-buffer requirement. Test actual device support and correctness.

If the required SF2 shader/memory semantics cannot be implemented correctly or
performantly through the selected MoltenVK/device combination, decide between
explicitly narrowing supported hardware, implementing the missing adaptation in
the Vulkan backend, or adding a native Metal backend. Importing another engine
or restoring unrelated upstream Zelda hooks is not a solution to that decision.

## Shared game blockers on every OS

- `runtime/dispatch.c:305-317`: guest `__cxa_throw` currently terminates. Correct
  exception selection, guest state restoration, cleanup and original catch/landing
  pads are needed where this game uses them. Abandoning callbacks or swallowing
  exceptions is not equivalent to the original catch path.
- `runtime/hle/movie.c:14-22`: movie input/track operations deliberately fail.
  Included cinematics need decoding, output, A/V synchronization and skip behavior.
- `runtime/nvn/vulkan/exec.c:3325-3327,3699-3704`: disabling guest shader memory
  or failing import can make global reads return zero. Such a workaround may
  permit a menu capture; it cannot establish faithful full-game rendering.
- Hardware rendering, combat, transitions, progression and persistence require
  actual playthrough evidence. Shader compilations or frame counters do not
  substitute for inspecting correct output and behavior.

## What “complete on one OS” means

Choose an explicit supported build (currently update v131072 / official display version 1.1.0), OS/CPU,
GPU/driver baseline, and minimum hardware. A Linux claim does not cover every
Linux GPU, and an Intel-macOS claim does not cover Apple Silicon.

| Milestone | Evidence needed | Current established position |
|---|---|---|
| Builds | Fresh user inputs pass identity checks and produce a native executable and local asset folder. | Local build/package and targeted tests exist; repeat from a clean machine before release. |
| Boots/renders | Original splash/menu draws, reacts to input and enters Story. | Verified on Linux software Vulkan. |
| Playable | Real combat runs at correct game speed, rounds end, transitions work and progress survives restart. | Not established by the menu/Story captures. |
| Full game for the declared build/platform | Every acceptance item below is passed, including complete progression and every included mode/content family. | Not established. |

The full-game release gate is:

- [ ] **Reproducible conversion:** a clean machine follows the documented setup,
  supplies its supported NSP/keys locally, and produces a runnable folder without
  personal paths, cached game intermediates or preexisting developer binaries.
  Wrong versions and incomplete input fail before replacing output. No keys,
  tickets or secret values appear in public artifacts or normal logs.
- [ ] **Content inventory:** compare with this exact Switch build and enumerate
  included gameplay/content first. The observed menu has Story, Sparring,
  Settings and Extras. Do not use the mobile game's current feature list as the
  Switch acceptance inventory. DLC/online features must be verified present and
  intentionally scoped; the runtime's `aoc:u` currently reports no DLC.
- [ ] **Full Story progression:** start a genuinely fresh profile, complete its
  tutorial and all progression through the actual ending, and revisit earlier
  content. Exercise unlocks, equipment/skills, rewards, defeats/retries and scene
  changes present in the declared build. Keep evidence of progression and the
  ending; do not substitute a pre-unlocked save for the complete run.
- [ ] **Sparring and remaining modes:** exercise the configurations that the
  original supported build actually offers. Finish matches, repeat/rematch, exit
  back to menu and switch modes. Verify controller assignment if the reference
  mode supports multiple players; do not invent an unsupported player count.
- [ ] **Combat correctness:** movement, attacks, blocking, collisions/hitboxes,
  AI, timers, health, round results, animations and representative unlocked
  equipment/effects agree with a reference run. No fake objects, instant loading
  completion, zeroed shader memory or swallowed exceptions are needed.
- [ ] **Persistence:** save new progress, quit fully, relaunch and continue; repeat
  after losses/rewards and mode/settings changes. Verify intended reset/new-game
  behavior, preservation of existing output saves during rebuild, and useful
  handling of write failures. No stale source version is silently loaded.
- [ ] **Settings and input:** edit copied INI after conversion and verify its next
  launch without recompilation; check language, volume/reverb, fullscreen and
  remapped keyboard/gamepad/triggers. Test reconnect/focus loss and key releases,
  plus each retained enhancement separately from the default presentation.
- [ ] **Sound/video/Extras:** compare music, effects, mixes and timing across play;
  verify included cinematics can play and skip correctly, remain synchronized,
  and return to the game. Inspect each included Extras screen/content type.
- [ ] **Rendering and performance:** use correct GPU memory semantics on supported
  hardware, visually inspect combat/background/UI/transitions/effects, and check
  clipping, scaling, resize and fullscreen. Match the reference gameplay speed;
  publish measured frame times and requirements rather than inventing a frame
  rate the original build has not established.
- [ ] **Stability:** perform the complete run plus repeated combat/menu/save/load
  transitions, cold/warm shader-cache runs and extended sessions. Investigate
  crashes, deadlocks, save corruption, progressive resource leaks and validation
  errors. Missing features are recorded as limitations, not presented as success.
- [ ] **Release record:** retain source/build IDs, compiler/dependency versions,
  OS/CPU/GPU/driver details, session logs and content/checkpoint results. Public
  evidence must avoid redistributing game assets or private data. Any included
  gameplay feature still omitted prevents an unqualified full-game claim.

Windows and macOS should reuse this same gate after their host layers work.
Finishing one OS first is a concrete scope decision; changing platform before
shared exception/combat bugs are understood does not remove those bugs.
