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
- The DOTween safe-mode catch in generated `f_700113c300` was the original
  failure point. The runtime now matches its LSDA type and resumes its generated
  landing pad; cleanup/rethrow behavior and full combat remain unverified.

## 2026-10-30 — first visually verified menu

- Working run: `build/review/software-x11/lasrecomp.log`, software Vulkan (lavapipe),
  SDL X11, `LARECOMP_VK_NO_GMEM=1`; no unsafe boot, swallowed exceptions, null-vcall
  grace, or SDK-trap grace. Captured splash at frame 13 and animated main menu at
  frames 60/120. Images inspected directly, not inferred from present counters.
- Menu run passed 5,000 frames. It later raised a real managed NRE in
  `Transform::set_localScale` (`7001a5cdf0`), called by DOTween's Vector3 tweener
  (`700113c300` -> `7000a5f7b0` -> `7000a5a480`). The October 30 follow-up
  reproduces and catches this exception at its guest landing pad. Input beyond
  the scripted Story dialogue and combat remain unverified.
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

## 2026-10-31 — uncommitted-change review and intro regression

- Plain `make` and conversion now run native setup with `--ensure`: reuse a
  complete local/system SDL, or download, verify and build pinned SDL 3.4.16.
  Verified from the missing local prefix through installation and a normal build.
- Movie audio must drain FFmpeg before frame zero is presented. Waiting before
  reading filled the audio pipe and prevented FFmpeg from producing video.
- Removed synthetic `FireMovieEvent` injection from graphics presents. End-of-stream
  injection reproduced a null access in `SwitchVideoPlayerController` after the
  original guest lifecycle had already progressed. The host overlay now ends
  independently and reveals the original guest scene.
- Normal-speed intro completed with software Vulkan/X11 and SDL dummy audio;
  the window was inspected during the movie and afterward in Shadow/Kenji combat.
  This verifies that transition, not full combat or audible device output.
- All 39 tests passed, including a pipe-drain startup regression; existing input
  and package tests were updated for the two-player state and FFmpeg bundle.
- Follow-up review fixed LSDA cleanup/type-filter parsing and null catch-all
  entries. Generated lookup respects clause order; native nested-unwind tests
  verify selector values, register restoration and frame lifetime. Main/SDK were
  regenerated and rebuilt. Cleanup-only unwinding and rethrow remain unsupported.
- The follow-up also blocks guest input behind the movie, validates the decoder
  bundle before replacing packaged files, and removes duplicate upload geometry
  and SDL flag selection. All 47 tests pass; a software-Vulkan launch completed
  the accelerated intro, visibly returned to the main menu and continued after
  scripted Story inputs without a fatal error. The packaged output was refreshed.


## 2026-10-31 — keyboard intro skip and native prompt labels

- The host movie now accepts the original skip actions, HintID A/B, through
  keyboard/gamepad bindings before guest input is blocked. Decoder teardown runs
  on the UI thread, and the triggering press is consumed through its release.
- Image-only `HintUI`, `ButtonHintUI` and generic graphic hints now receive a plain
  Unity Text child using the game's font and font size. Direction groups share
  one label; a layout element reserves the measured text width. Controller mode
  restores original artwork and disables the added text/layout. Destroy hooks
  release cached hint records when their guest components are destroyed.
- Verified with the actual INI: J skipped the normal-speed intro, the menu showed
  `Up / Down Select` and `K OK`, and K opened Story with `MORE K`. A temporary
  diagnostic INI rebound A to Space to verify that both labels and skip followed
  the mapping; no Space binding was added to the user's INI.
- Keyboard label color comes from the replaced sprite's dominant opaque artwork
  color, multiplied by its own Graphic tint; later tint and alpha changes follow
  that original graphic. Captions supply only the font. This matters because the
  dark button beside MORE has a white tint with dark pixels baked into its BC3
  atlas. Sampling uses the sprite rectangle in the original NVN backing storage,
  ignores transparent padding, and caches the result until the sprite changes.
  Unsupported formats retain the original tint. Regression tests cover atlas
  bounds, tiled RGBA/BC3 palettes, transparency, mapping, restoration and fading.
- Visually verified dark `MORE K` and cream main-menu keys with the actual INI.
  All 49 tests pass; the packaged build was refreshed while preserving its INI.
- Keys inside buttons with a separate visible background now have outline badges
  in the same color as the mapped key: circles for single characters and rounded
  rectangles for longer names. The key is sized to fit within the border and the
  layout reserves its measured width. Standalone footer/header hints stay plain,
  including legends that happen to have invisible Button components. Borders use
  feathered Unity UI mesh geometry, following normal canvas clipping and fades;
  controller mode disables them along with the generated labels.
- The hint's LayoutElement now lives on the direct child of the nearest horizontal
  layout, so long labels reserve space beside the caption. Fixed icon slots keep
  the badge's left edge in place. Launch checks covered circled K and boxed Space
  and Backspace; the temporary word bindings did not change the user's INI.
- All 49 tests pass and the packaged executable matches the rebuilt runtime.
  One final normal-binding launch stalled during Story loading: the main thread
  waited on condvar 0x7240e5d628 from guest frame 0x7001a81eec, with the render queue
  fully drained. An identical retry reached the Sensei dialog and rendered the
  circled K. This intermittent scene-loading stall remains unresolved; its trace
  is retained at build/validation/outline-story-stall.log.

## 2026-10-31 — experimental keyboard hints default off

- All in-game keyboard prompt replacements, including inline text, image labels,
  badge outlines and toggle changes, now require `[experimental] keyboard_prompts
  = true`. The runtime default, supplied INI and existing local build/package INIs
  set it to false. `prompts = keyboard` alone does not opt in. Keyboard mappings
  and movie skip remain independent of this display feature.
- Inline dialogue preserves the game's directional shorthand instead of expanding
  it into key lists. Standalone direction groups use `WASD`, `Arrows`, or compact
  numpad names; other names retain familiar abbreviations (`Esc`, `Enter`, `Bksp`).
  Generated text rectangles follow measured content, and unusually wide names
  shrink to a bounded width rather than expanding the entire dialog row.
- All 50 tests pass, including default/INI opt-in behavior, UI restoration,
  directional shorthand and width limits. Launches verified the original icons
  with the default INI and compact prompts through the punching-bag tutorial with
  a temporary opt-in INI. The package was refreshed with the experiment disabled.

## 2026-10-31 — optional hooks bypass disabled callbacks

- Keyboard hooks are now a separate runtime group configured after INI loading
  and before guest threads start. Each generated optional site checks its group
  bit before calling the hook; when disabled it executes the original, validated
  instruction directly. Console and frame-rate compatibility hooks remain active.
- Hint caches and cached Unity types moved from fixed global arrays into one
  lazily allocated state block. Disabled runs allocate no hint state or UI and
  perform no keyboard-hook lookups/scans. Reset frees the state. The executable
  still contains the feature code and a flag check at each optional site.
- Generator regression tests execute original and gated instructions and count
  callbacks. Runtime tests exercise every disabled site with invalid guest
  pointers, verify CPU state is preserved, check lazy allocation/reset and confirm
  that frame-rate hooks remain active. All 52 tests pass.
- Regenerated/rebuilt main and SDK. Both launch checks skipped the intro using J
  and entered Story with K: disabled mode showed the original A icon, while opt-in
  mode showed the circled K. The packaged executable was refreshed with the
  experiment still disabled in its INI.


### 2026-10-31 — live play: shared-font wait and missing half comparison

The live session in `build/validation/play-session-20261031-104311` froze
returning from gameplay to the menu. Its automatic hang dump shows presentation
stopped with the render queue drained. The main guest thread repeatedly queried
`pl:u` command 1; the generic successful service stub returned zero (not loaded)
forever. This is distinct from the earlier intermittent Story loading wait.
The retained `freeze-diagnostic.log` includes the thread stacks and preceding
font-service polls. External debugger attachment was unavailable, so diagnosis
used the runtime's built-in hang dump.

Added the `pl:u` service, which loads pinned OFL Noto Sans/CJK substitutes before
returning loaded state and exposes their actual sizes, offsets and shared-memory
bytes. Fonts are loaded only on demand and freed at reboot reset. The build and
conversion pipelines prepare/copy the fonts and licenses automatically. This
uses the normal runtime service interface without changing guest game logic.
The Nintendo private icon font is not reproduced; its slot uses Noto Sans.

The same session also logged an unsupported fragment instruction
`7e83038400070307` at shader PC `0x78`, followed by skipped draws. It is the
constant-buffer HSETP2 form, previously missing from the translator. Added the
constant/immediate HSETP2 and HSET2 forms using their distinct comparison and
H_AND/BF field positions. The exact logged instruction now translates and its
GLSL compiles to SPIR-V; register-form behavior is covered as well. This error
alone did not establish the cause of the earlier empty dialog/freeze.

All 56 tests pass, including shared-font data/ranges/reset, bounded output lists,
checksum rejection, packaging preservation and half-comparison translation.
A hidden AMD Vulkan run using a separate save copy reproduced font requests
while leaving the Dojo, mapped both fonts, and continued through a populated
Sensei level-up dialog into Profile. It exited normally after 12,000 frames;
no hang or shader-translation failures were logged. Screenshots and log:
`build/validation/menu-return-font-check`. This is one transition test, not a
full-game stability claim.


### 2026-10-31 — uncapped presentation and resolution checks

Live play reported fast blinking. The log confirmed 650–1,300 guest presents
per second despite `target_fps = 60`: NVN stored the window presentation interval
but never waited for it. The mailbox swapchain did not throttle those submissions.
Added per-window monotonic presentation deadlines at the NVN boundary. Positive
intervals multiply the configured virtual display period; interval zero still
respects the configured FPS cap. Late frames rebase instead of bursting to catch
up. No guest animation or game timing code was patched.

The test build held about 59.7–59.9 FPS after loading. Added an actual NVN-entry
regression test for the cap, interval changes, late-frame recovery and invalid
presents. Scripted window resize requests had also been lost when the overlay
was removed; the UI stub now applies those requests on the UI thread.

Enlargement blur was reported from a previous run, not the current live session.
Automatic/native resolution followed scripted resize correctly: actual Vulkan
render targets changed from 1280x720 to 1920x1080, 960x540 and 2560x1440. Added
`[output]` logging for actual source and window dimensions so future reports can
be checked against real allocations. `build/validation/pacing-resize-check`
contains the log and final 2560x1440 capture. A separate fixed-2x test rendered
at 3840x2160 into a 1920x1080 window; a fractional-resolution FSR test rendered
at 960x540 and logged the FSR pass to 1920x1080. Both exited normally; logs and
captures are in `pacing-fixed-2x-check` and `pacing-fsr-half-check` beside the
resize test. Bitmap artwork/font atlas detail is independent of render-target
resolution; these tests did not reproduce the earlier scaling complaint.

All 57 tests pass. These checks cover the presentation API and selected scenes,
not full-game timing/stability at every configured frame rate.

## 2026-10-31 — Radeon driver-owned guest heap

RADV now requires driver-owned coherent heap backing by default and disables
external host-pointer imports, their whole-heap probe and lazy userptr chunks.
The buffer is bound before the guest mapping is published; write tracking is
disabled before replacing anonymous pages with the DRM mapping. Reset removes
the CPU mapping before freeing its Vulkan allocation. Allocation/mapping failure
stops boot instead of silently falling back to the previously faulting userptr
path. No `NO_GMEM` zero-read fallback is used in the hardware tests.

The Radeon 680M (Mesa RADV 25.2.8, Linux 7.0.0-34, X11) reached 1,200 frames,
including the animated menu, Story dialogue and Sensei movement tutorial, with
strict Khronos validation enabled and no reported validation errors. The first
validation attempt identified uninitialized dynamic stencil operations on draws
with stencil disabled; initializing the declared state fixed that error. Warm
sections of the successful run logged 59.8 FPS at 1280x720, with loading/shader
compilation dips. The final scene was visually checked. This does not establish
full combat or full-game compatibility.

`tools/test_gpu_heap.py` compiles the actual production heap allocator into an
isolated Vulkan compute test, with no game inputs. CPU-written data at the first,
middle and last heap pages is modified by the GPU and checked on the CPU after
a host-read barrier and fence; a fresh allocation repeats the test and checks
zero initialization. Both cycles passed on the Radeon with the full 3,328 MiB
heap. Regular regression tests also cover allocation failure stopping boot and
real global-memory binding when host-pointer import support is disabled.

Evidence: `build/validation/gpu-amd-story`,
`build/validation/gpu-amd-validation-fixed`,
`build/validation/gpu-heap-roundtrip.log`, and
`build/validation/gpu-unit-tests.log` (61 tests passed).

A longer scripted run subsequently reached frame 3,000 and entered a Kenji
sparring fight using the current 120 FPS repository settings. Captures at frames
2,400 and 3,000 show both fighters and combat animation. Movement and attack
inputs were scripted, strict validation reported no errors, and the process
exited normally. Warm sections logged about 119.2 FPS; shader compilation and
loading caused dips. This checks one combat scene, not complete fights or the
full campaign. Evidence: `build/validation/gpu-amd-tutorial`, including
`environment.json`, `result.json`, logs and screenshots.

The RTX 4070 is physically present and NVIDIA 595.71.05 is installed. Its kernel
module is installed for 6.17.0-35, but not the running 7.0.0-34 kernel:
`modinfo nvidia` fails, PCI has no bound NVIDIA driver, and `nvidia-smi` fails.
An explicit NVIDIA-ICD test failed Vulkan physical-device enumeration before
allocating any memory (`build/validation/gpu-nvidia-attempt.log`). Dedicated-GPU
validation therefore remains blocked by host driver availability. Secure Boot
is enabled; a repair must use an appropriately signed matching module. No
system packages, kernel modules or boot settings were changed.

The installed module for 6.17.0-35 is NVIDIA 595.71.05 and is signed by
Canonical Ltd. Kernel Module Signing; the matching kernel and initramfs are
present. Booting that kernel is a concrete option for a subsequent RTX test,
subject to verifying `nvidia-smi` and Vulkan enumeration after boot. The current
7.0.0-34 module package available in APT requires a different NVIDIA version
than the installed driver, and an installation simulation did not resolve its
dependencies. No driver repair or reboot was attempted.

## 2026-10-31 — RTX 4070 validated after driver repair and reboot

NVIDIA was subsequently upgraded to 595.99.02 with Canonical-signed modules for
7.0.0-38, and GRUB's saved Ubuntu entry was set to that kernel. After the user's
reboot, the running kernel is 7.0.0-38, PCI reports the NVIDIA driver bound to the
RTX 4070 Laptop GPU, and both `nvidia-smi` and Vulkan enumerate it normally.

The isolated production-allocator compute test passed two full 3,328 MiB
driver-owned heap allocation cycles on NVIDIA, including CPU-to-shader-to-CPU
visibility at beginning/middle/end. The game itself was tested with its default
NVIDIA host-import path (3,236 MiB imported for shader global memory), strict
Khronos validation, an isolated settings snapshot and separate saves. It reached
frame 3,000 in Kenji sparring and exited with status 0, with no reported Vulkan
validation errors. Captures at frames 2,400 and 3,000 were visually inspected;
movement, attacks, health changes and knockdown are visible. `nvidia-smi` listed
`sf2` using the dedicated GPU during the test.

This NVIDIA/X11 run selected FIFO presentation and logged about 59.9 FPS in warm
sections at 1280x720, despite the 120 FPS target. This is not a matched performance
comparison with the AMD mailbox run; shader/loading dips also occurred. Full-game
stability remains unverified. Evidence: `build/validation/gpu-nvidia-post-reboot`
(heap log, settings/environment, game log, result JSON and screenshots).
