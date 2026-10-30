# find findings — SF2 recomp working notes

Persistent store for cross-cycle findings that don't fit one commit message (or that
arrived as instrument output after the instrument's commit). Newest first.

## 2026-10-30 — official identity, measured performance and persistent saves

- Corrected display-version assumption: base NACP is 1.0.0, update NACP is 1.1.0.
  CNMT update revision 131072 is not a human version. Updated code still has main
  build E15FC665EC42684AA6EDD1AE03624D96; merged RomFS/update extraction is applied.
  Older v2.0.0 wording below is historical and incorrect.
- Extracted update Control NCA locally: official title/publisher/NACP/icon only
  go into ignored game/ and converted output. SDL/native Vulkan app metadata use
  the official display version. Native window title, class/app ID and 256x256 icon
  were read back from the actual X11 application window. Matching desktop entry
  installed for this user's converted copy. No game assets were added to Git.
- Fixed stale Zelda add-on-content base ID; the runtime now reads the original
  NACP field (this update: 0100AB000ABFF000). DLC availability is still unsupported.
- Removed remaining widened-lighting Zelda shader patch. Guarded CPU-side buffer
  copies avoid waits only without imported/lazy heaps and with overlap enabled;
  staged snapshots invalidate after writes, imported paths retain synchronization.
  Matched warm Story: 49.38 -> 55.87 FPS (+13.1%); no hardware/combat performance
  claim. Details in docs/performance-notes.md; real helper/cache regression added.
- [storage] save_directory supports validated absolute/relative runtime paths,
  quoted spaces, and LARECOMP_SAVE_DIR precedence. Fixed nested abandoned pending
  cleanup without following directory links outside the selected mount.
- Copied supplied unlocked save only into build/review/persistent-save. Repeated
  processes read/write the configured path; 1014 XML elements/profile ID and
  progression remained, ignoring regenerated fight seeds/timers. Stored version
  upgraded from 1.0.0 to 1.1.0. Original backup hashes unchanged, no pending files
  left. Source save XML declares UTF-16 but is UTF-8 text; the original game's
  text loader handles it, so validation parsed decoded text without altering it.
- Full build/package and 35 tests pass. The platform/complete-game acceptance
  checklist is in docs/portability-and-validation.md. Shared exception landing
  pads, video, correct native GPU memory and verified combat/progression remain.

## 2026-10-30 — source distribution, targeted cleanup and portable runtime INI

- Removed Zelda cosmetic/tint/texture/pot/bottle/pet/Hint Sheet/mirror/widescreen
  policies and the old pacing controller. Generic Vulkan/NVN/Maxwell compatibility,
  scaling, SMAA/FSR, filters, recorder, profiling and caches stay. Shader cache
  version incremented; the float/bool viewport-hook ABI bug is gone with that hook.
- Removed randomizer RomFS overlays and extended-save-flag sidecars, preserving
  ordinary RomFS/saves and tested pending-file commit semantics. Removed dormant
  launcher migration/delete APIs; legacy marker files cannot trigger data deletion.
- Generated main/SDK tables now contain their complete source NSO IDs. Runtime
  checks both modules before starting graphics; base builds are no longer falsely
  accepted as compatible with update-specific hooks/code.
- INI keyboard/gamepad mappings load at runtime. Paths default to executable-side
  INI/state, independent of launch working directory; explicit config and data-root
  overrides remain. Conversion copies default INI without overwriting user edits.
- tools/convert.py provides the local extraction/generation/build/package flow.
  Native output contains the user's translated executable and copied original
  data; public source distribution excludes those files and all user keys.
- Extraction rebuilt from pruned shared sources: all 49 files from a newly staged
  real-NSP extraction matched the working inputs. Official-source Il2CppDumper
  reproduced the 90,105-method / 83,673-address map byte-for-byte.
- Fixed Bash-under-sh extraction, personal key path, plaintext key logging,
  incorrect filesystem-section field lookup and upstream Dumper argument count.
  Unsupported layouts fail before replacing data; staged publish rolls back.
- Removed 1,013 nested duplicate dependency files and source-distributed compiled
  tool binaries; local builds remain ignored. Added pinned tool procurement,
  dependency checks, component notices and a root MIT license.
- Full tools/convert.py --nsp invocation completed with source-built dependencies
  and produced out/shadow-fight-2-linux. A re-conversion preserved the edited INI.
  Final executable launched from /tmp, loaded its own INI, and used Space mapped
  to A to reach Sensei; captures 60/450 inspected. Restored the test output's
  default K mapping afterward. The run passed 1,300 frames; shader variant
  recording remained active. All 21 regression tests passed.
- Pinned SDL source build/probe verified in an isolated prefix. Cleaned-runtime
  software-Vulkan captures at frames 60/450 showed menu/Sensei after over 1,200
  frames. Original guest exception landing pads, combat, video and Windows remain
  unfinished; hardware-driver faults are not resolved by this cleanup.

## 2026-10-30 — remove obsolete boot workarounds

- Removed fake empty scenes, fabricated component bindings, forced loading
  completion, pair-cell experiments, arbitrary callback abandonment, worker
  restarts and fault-as-raise behavior. These masked the memory corruption rather
  than implementing the original game. The unused override API and its cache
  machinery are gone too; historical fixes/tests remain in git history.
- Verified the original Unity internal-call resolver works after the page-touch
  correction. Removed the duplicate host registry/cache and hardcoded map dumps.
- Reduced `hooks.c` to six console-stream compatibility sites. Removed temporary
  constructor/trampoline probes and unused Link-health controller LED code from
  the upstream runtime. Generic crash reporting and optional address watches stay.
- Regenerated main and SDK from the extracted original NSOs. A 50-second run in
  `build/review/lean-original-resolver` passed 2,300 frames, reached Story, and
  exited through the test timeout. English menu (60) and Sensei dialogue (450)
  screenshots were inspected directly. Software Vulkan/X11 was used.
- After deleting the now-unused resolver code and LED remnants, another run in
  `build/review/final-cleanup` passed 1,600 frames in 35 seconds. Menu (60) and
  Sensei (450) captures were inspected. A final 30-second run after simplifying
  crash reporting passed 1,200 frames with A presses at 180/400/700; it did not
  reproduce the earlier DOTween exception within that window. This does not
  validate exception handling or combat.
- The remaining regression tests cover actual host-page materialization without
  data corruption, locale selection, and extensible INI configuration/precedence.
- Earlier notes below describe historical experiments, including removed flags;
  they do not describe features that should be restored. Real exception landing
  pads, hardware-driver validation, video playback and combat are still unfinished.

## 2026-10-30 — follow-up audit and editable configuration

- `tools/run_linux.sh --software` with scripted A presses at frames 180/400
  reaches English Story/Sensei dialogue (frame 450 inspected). A subsequent
  50-second audited run reached frame 1,000+ in the story with no swallowed raises.
- Fixed dispatch cache pollution: a probe's call to the original function could
  cache that body and bypass the probe afterward. Nested probes also cleared the
  outer bypass identity. Actual-function regression test reproduced both bugs.
- Fixed locale precedence, POSIX locale normalization, automatic/Japanese enum
  collision, and the oversized Korean identifier. All emitted language identifiers
  now fit the IPC field. Config and environment precedence are tested.
- `sf2recomp.ini` is the user-editable INI configuration. Supported sections:
  general.language, audio.volume/reverb, video.fullscreen. A single typed option
  table controls supported keys; unknown sections/keys are forward-compatible.
  The launcher selects the project config through absolute `SF2_CONFIG`.
- Fixed Makefile recipe overlap that silently replaced six intended `-O0` builds
  with the generic `-O1` rule (dry-run checked both slow and ordinary chunks).
- Per-draw and render-queue step logging now requires `SF2_TRACE_RENDER`; detailed
  guest wrapper probes require `SF2_TRACE_GUEST`. Ordinary stats/errors stay on.
- The real DOTween safe-mode catch code already exists after the normal return in
  generated `f_700113c300`; the generator currently only extracts function starts
  from EH metadata and does not connect raises to landing pads. This is the proper
  exception-handling work remaining, not another null Transform stub.

## 2026-10-30 — first visually verified menu

- Working run: `build/review/software-x11/lasrecomp.log`, software Vulkan (lavapipe),
  SDL X11, `LARECOMP_VK_NO_GMEM=1`; no unsafe boot, swallowed exceptions, null-vcall
  grace, or SDK-trap grace. Captured splash at frame 13 and animated main menu at
  frames 60/120. Images inspected directly, not inferred from present counters.
- Menu run passed 5,000 frames. It later raised a real managed NRE in
  `Transform::set_localScale` (`7001a5cdf0`), called by DOTween's Vector3 tweener
  (`700113c300` -> `7000a5f7b0` -> `7000a5a480`). Guest exceptions currently abort
  instead of running Unity's callback catch path. Input/story and combat remain
  unverified; do not suppress this exception and count it as fixed.
- Ordered GPU setup successfully selected a DMA-BUF heap, but hardware submission
  still killed the render thread in the AMD kernel's HMM callback. Software Vulkan
  through X11 avoids that path; software Vulkan through Wayland did not establish
  a working presentation in the bounded test. Reproducible launcher:
  `tools/run_linux.sh --software` (English by default).

## 2026-10-30 — correctness review supersedes earlier conclusions

- `runtime/nvn/vulkan/exec.c:pretouch_host` was corrupting live guest memory with
  `*page = 0`. The observed constructor-pointer low-byte change is exactly consistent
  with this write. The isolated regression test fails against that implementation.
  Volatile atomic OR-zero preserves contents and remains a real write at `-O2`;
  actual Linux host-page spacing is required, not the guest's 16 KiB `PLAT_PAGE`.
- AMD's `amdgpu_hmm_invalidate_gfx` faults were independently recorded in the host
  kernel log during live host-pointer imports. Avoiding data corruption alone does
  not make that driver path reliable.
- The runtime omitted `backend_guest_heap_prepare` entirely. The UI/device now starts
  before the guest thread, and first `SetHeapSize` calls heap preparation before SDK
  allocations. The ordered-boot run confirms driver memory through DMA-BUF:
  `3328 MB at 7240000000`, avoiding live anonymous-page imports on this machine.
- Original Unity internal calls resolve from the real 5717-entry registry in strict
  runs. Broad fake-engine fallback behavior and initialization overrides are now
  explicitly opt-in (`SF2_UNSAFE_BOOT=1`); they are not correctness fixes.
- The earlier three empty slots were probed at WRONG addresses outside guest memory.
  Correct addresses, checked against original ARM64 loads, are `70039848e0`,
  `70039848e8`, `70039844a0`. Failed probes now report UNREADABLE. The old zero-slot
  hypothesis is invalid until remeasured.
- Existing IL2CPP symbols identify `70015d10a0` as exception-to-string formatting,
  `7001268450` as GetComponentsInParent, and `70005c9500` as GetRectMaskForClippable.
  The destructive load-step override has been removed. `700121e720` calls
  `UnityEngine.Component::get_gameObject()` (literal read from the NSO), rather than
  merely returning an unresolved dispatch thunk.
- The experimental host longjmp path now restores the separately stored guest CPU
  snapshot. It previously left SP/callee registers corrupted. Tests cover nested
  recovery, retained memory effects, and anchor cleanup at `-O0`/`-O2`. This is still
  callback abandonment, not guest C++ exception handling.

The older notes below are historical observations/hypotheses and must not be used
as an instruction to restore bypasses or assume their conclusions remain valid.

## 2026-10-30 — the NRE wall, current frontier (head d5666c4/bfef452 + star run)

State: boot runs clean to the NRE exit wall: `System.NullReferenceException` raised on the
binding walker B chain, eaten to anchor 0, nnMain returns 0. 63 commits.

### Proven this cycle (star run `lasreg_star.log`, gen_b2c/star)
- `[bind2b] claim` rows (24 cap): the claim helper `f_7000b2e720` runs from MANY caller sites
  (lr `7000b5b5ac` dominant, plus `70015d0594/5cc` = loadstep-function family). Every logged
  manager object is alive: `[x0]` (slot0) = klass `7240a3b000`/`a3b1c0`/`a3b080` (IL2CPP
  klasses), `[+0x122]` arm byte = 0x8/0x9, `[+0xd4]` claim flag 0/1. **The binding manager is
  NOT the empty thing** — hypothesis dead.
- `wrap 1` (raise wrapper `f_7001268450` entry): x0 = `7240789e80` (VALID object), x1 = 0,
  x2 = `72dadeec30`, x3 = `72daf2c690`, lr `70005c95e8`. The wrapper saves x0 in x19 and
  tail-calls (`br x1`) with x0 PRESERVED — yet the raiser later receives x0 = 0 (raise-op 2:
  x1 `7240b68000`, x2 1, x3 1, x4 0, x5 1, x6 `72dadeec30`). x1 argument is rewritten by the
  wrapper (`mov w21,w1; mov x1,xzr`) so raise-op's x1/x6 differ from the 95e4-bl site's args.
- Wrapper's resolve: `bl f_700121e720(x0, 0)` = bridge resolver reading slot `[[7003b8bb70]]`
  (7003b88b0 + 0xc0), string key at `0x7002cd4d3f`, fallback through the icall resolver
  `f_7000b7cd80` (the HOOKS-covered icall row) — after which `ldr x3,[x19+0x30]`, `x4 = [x3]`,
  `br x4` — a thunk-table dispatch. The 0-manufacturing point lives in that thunk subtree
  (not yet decoded).
- `[gdump]` at the death instant: healthy = `[[39b0cd8]] -> 7003b33750` (inner `72daf2b7c0`),
  `[[39b0ce0]] -> 7003b44100` (inner `72daf2b9d8`), `[[39b0ca0]] -> 7003b33738` (inner
  `7241d2f4d0`), `[[39b0ce8]] -> 7003b44108` (inner `72daf2c690`). **EMPTY (0): `[[700398408e0]]`,
  `[[700398408e8]]`, `[[700398404a0]]`** — three registry-slot holders.
- No `str/stur/strb/stp` with immediates 0x8e0/0x8e8/0x4a0 anywhere in the NSO text
  (capstone scan to 29 MB, 0 hits): those three slots have NO direct guest-code initializer —
  filled indirectly (or by data init we didn't map). `[[3984 8e0]] = 0` also means the
  `ln r 9588 ldr x0,[x8]` path (`ldr x8,[700398408e0]` then `[x8]`) would fault — it cannot be
  the live path since the boot survived to the wrapper: at CALL time the slot wasn't 0 yet.
- NSO0 header layout: text at fileOff 0x100, size 0x2375908; seg table parse (for other
  segments) still TODO — needed only if raw-file statics checks are required again.

### Prepped, not applied (words fetched, edits not made)
- hook site `0x70005c95e4` (the bl into the wrapper) — word `0x94327b9b`.
- hook site `0x700121e720` (bridge resolver entry) — word `0xd100c3ff`.
- Planned: wrap tracer live-probes all seven gdump slots at wrapper entry; claim cap 24→48.
- Purpose: catch the pre-tail args and the slot states **at call time** (gdump only sees death
  instant) to decide between "slot emptied mid-boot" vs "bridge/thunk manufactures the 0".

### Decisions
- gdump holder list stays as-is (proved: probe reads those pages fine).
- The loadstep null-serve connection stays OPEN: `[[3984 4a0]]` is empty AND `[aotreg]
  7003beffb0 (+0x20: 7003927208)` printed by the loadstep guard is a different global, so the
  two stories are adjacent but not the same object.
