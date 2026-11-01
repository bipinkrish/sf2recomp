# LAN Sparring

The converted game includes its own native LAN menus and discovery. Players
launch the game normally; Python, pip, scripts and a separate launcher are not
required. Each computer renders its own video/audio and exchanges logical
fighter inputs and combat-state checks.

## Play from the game

1. Connect both computers to the same local network and start the same game build.
2. Select **Sparring**. Choose **Normal** for the original local mode, or
   **LAN Multiplayer**.
3. On one computer choose **Host**. It opens the original character-selection
   screen and waits for player 2.
4. On the other computer choose **Join**, then select the host in the discovered
   host list. **Refresh** searches again.
5. Each player uses their normal local player-1 keyboard/controller controls.
   The host controls fighter 1; the guest controls fighter 2. Complete character
   and arena selection to begin.

Normal/LAN and Host/Join are fixed choices with no scrolling or scrollbar. A
large discovered-host list can scroll. Back shows the mapped keyboard/controller
input and remains available throughout setup.

The TCP connection survives a completed match. **Restart** returns both players
to character selection on the same connection. Leaving Sparring closes the
session. A disconnect displays an in-game message and allows returning to the
main menu; it does not close the application.

Graphics remain local, including FPS, aspect ratio, internal resolution,
upscaler and antialiasing. The transport uses two steps of input delay at a
60-step combat clock. It uses lockstep without prediction or rollback.

## Configuration

The supplied INI contains:

```ini
[gameplay]
allow_sparring_lan_multiplayer = true
```

This defaults to true even in an older INI that lacks the key. Set it to false to
open the original Sparring path directly, without the Normal/LAN choice.

This is independent of `[network] enabled`, which controls the original game's
publisher/server paths. Those paths remain disabled during LAN play. The native
LAN sockets open only after choosing Host or Join.

Host discovery uses UDP 34251; the game connection uses TCP 34250. The host's
firewall must allow those ports on the local network. Networks that isolate
Wi-Fi clients or block local broadcast can prevent discovery. No internet
service, account, manual IP entry or router port forwarding is used.

## Files needed on each computer

Copy the complete converted folder for the target operating system: executable,
`lib/` (SDL/FFmpeg/fonts), `extracted/` (matching ExeFS/RomFS), INI and normal save
data. The native UI uses the game's existing widgets and input mapping. There
are no Python or pip packages to install or bundle for LAN gameplay.

Use matching supported SF2 update v131072 (v1.1.0) data and the same runtime
revision on both peers. Linux executables require compatible Linux runtime
libraries and Vulkan drivers. Other operating systems require their own native
build; cross-platform gameplay is not yet hardware-verified.

## Synchronization and diagnostics

The host supplies the shared seed and concrete fighter/arena templates. Random
selections are resolved once on the host. Each Random fighter and arena uses a
separate draw from an advancing SplitMix64 menu-selection stream, with rejection
sampling over the eligible entries. The host sends the concrete choices; the
guest does not reroll them. This stream is separate from combat RNG and persists
through rematches. Normal Sparring retains its original RNG.

Each side uses the replay-tested
initialization and scoped RNG policy, with both fighter models marked human.
Menu input ownership comes from the connection role. Combat packets carry
bounded ordered input events and pointer-free state checks; remaining checks
are drained before the end-of-match handshake.

Checks cover observed health, equipment, model/node positions, animation and
controller histories, fight/round counters, PvP framing and controlled RNG.
These are not a complete state serializer or a proof that every engine
dependency is observed. Additional roster, arena, platform and physical-network
coverage remains useful.

The Python tools `tools/lan_sparring.py` and `tools/test_lan_sparring.py` remain
developer diagnostics for the older command-line single-match path. They are
not used or shipped as a requirement of the native game UI.
`LARECOMP_LAN_TRACE_DIR` optionally records native GUI matches for developers;
ordinary sessions use a bounded temporary checkpoint stream.

The investigations below retain the earlier command-line integration history.
Current native-menu validation is recorded under
`build/validation/native-lan-ui`.

## Native menu validation (2026-11-01)

The native-menu integration checks passed 133 regression tests. That real-game pair used the native Normal/LAN,
Host/Join, discovery, character and arena screens. Its completed two-round match
matched all **5,875 observed states**. Restart on the host returned both peers to
selection and started a second match on the original connection; all **741 states**
in the common recorded rematch prefix matched. That rematch was intentionally
interrupted to test disconnect recovery, so it is not a second completed match.

Closing the host window left the guest open with a connection message. The mapped
Back key returned it to the main menu, and Normal Sparring opened the original
character-selection/tutorial path. Both game processes exited 0. The converted
executable separately opened Host/Join, went Back and entered Normal Sparring with
`PATH=/nonexistent`; no Python interpreter or pip package was available to it.
The executable used for those checks had SHA-256:
`f195085ffa508eefd3c44b1c08834b63dbad548522acdddd7f9da0660daf0e79`.

The final checks also cover remapped keyboard/controller Back and navigation,
fixed two-option layouts without scrolling, and Random placeholders at any roster
position. Earlier trials exposed a Random-index assumption and a modal Back input
failure; both are fixed, with those trial logs retained. A concurrent socket-test
run interfered with an integration trial; it was invalidated and rerun separately.

Results, screenshots, failed trials, automation scripts and the final test/build
logs are retained under `build/validation/native-lan-ui`; `summary.json` identifies
the final evidence. These are two processes on one Linux/NVIDIA computer, not yet
a physical two-computer or cross-platform LAN test.

## Randomness quality checks (2026-11-01)

Random setup choices now use separate advancing host draws; repeated fighters are
allowed, and resolved fighter/arena indices are sent to the guest. The shared
combat seed initializes each scoped stream with a 64-bit avalanche mixer before
the original 31-bit LCG runs. This fixes a measured first-draw correlation between
two real combat call sites without replacing Nekki's RNG update or native
float/integer/chance conversions. The current policy is `scoped-callsite-lcg-v2`.
Both computers must update: the simulation identifier changed to reject peers
using the older seed policy. Normal Sparring retains its original RNG.

The headless comparison executes the actual recompiled Nekki RNG and the Unity
engine RNG functions registered by this game, with only state storage stubbed.
Eight seeds and one million samples per seed were checked per distribution.
There were **zero mismatches in 32 million aligned Nekki RNG/conversion checks**
(raw draws, floats, signed integer ranges and chance results). Aligning the
internal state compares generator/conversion behavior; it does not mean LAN
reproduces the original match's sequence of outcomes.

| Numerical check | Original Unity RNG | Updated LAN |
| --- | ---: | ---: |
| Float mean, expected 0.5 | 0.499988 | 0.500004 |
| Integer bucket chi-squared / degrees of freedom, near 1 expected | 1.055 | 0.932 |
| First-draw correlation across eight million seeds | 0.000209 | -0.000375 |
| Fighter-pair correlation in eight million setup triples | 0.000012 | 0.000020 (new menu stream) |

The previous combat seed mixing measured -0.085735 in the smaller 800,000-seed
check; the corrected mixing measured 0.000020 on that same check. The deliberately
broken old menu formula was detected: it missed 280 of 320 possible setup triples.
The new menu stream covered all 320 and passed the frequency/correlation checks.
Finite diagnostics do not prove independence or certify every RNG property.

Two headless socket peers also agreed on 4,096 actual LAN RNG outputs across
1,024 synchronized steps. All 135 regression tests pass, including repeated-seed
reproducibility, stream isolation and rejection of the older RNG policy. No new
UI validation is required for these numerical changes. The original Nekki
float-based integer conversion's rare upper-endpoint rounding is recorded by
the diagnostic rather than silently clamped or changed.

Reproduce with locally generated game code and build dependencies:

```sh
python3 tools/test_lan_randomness.py --samples 1000000 --output build/validation/my-rng-check
```

The output directory must not already exist. Results, original-function hashes,
the comparison harness, failed earlier measurements and headless network logs
are retained under `build/validation/lan-randomness`. The current report is
`final-quality-and-sync/results.json`. These developer diagnostics are not
included as a Python requirement of the converted game.

## Startup synchronization fixes

The startup investigation found a condition-variable publication race in `svc_wait_process_wide_key`. The runtime released the mutex before setting
the waiter key. A native notifier could acquire that mutex in its user-space fast
path, read a zero key, and skip the signal before the waiter finally registered.
Holding the host kernel lock did not protect that native fast path.

The key is now published before the mutex is released. A regression test runs
the real kernel code while forcing a competing notifier into precisely that
window. The old ordering loses the notification; the fixed ordering passes 1,000
iterations. Ten offline replay cases passed, but a later stress run reproduced
another graphics-worker stall, so that change alone was insufficient.

The CPU helpers also weakened ARM RCsc synchronization to C acquire/release.
That mapping permits a store to be observed after a subsequent acquire on x86,
contrary to the required guest ordering. The actual helpers reproduced the
forbidden store-buffering result 66 times in 300,000 iterations. Conservative
host sequentially consistent atomics produced zero occurrences in the same test.
A separate 200,000-iteration regression covers this ordering. The change applies
to all generated game/SDK code and requires a full rebuild. See
[Arm's explanation of RCsc ordering](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/when-a-barrier-does-not-block-the-pitfalls-of-partial-order)
and the [corresponding emulator translation issue](https://www.mail-archive.com/qemu-devel@nongnu.org/msg1218202.html).
No guest wait is bypassed and no timeout is treated as completion.

## Validation

`tools/test_lan_sparring.py` launches two real game processes with private saves,
configuration, logical input tapes and traces. `--ticks` on the normal launcher
also enables a bounded diagnostic trace. When a LAN match naturally ends before
that limit, a valid early-completion marker requires a complete state prefix and
a native game-over flag. Ordinary sessions retain only a bounded temporary stream
for state hashing instead of accumulating full traces.

Local evidence is under `build/validation/lan-integration`. The first dual-NVIDIA
attempt encountered a Vulkan loading timeout before combat; it is retained in
`lan-games-first` and is not a successful LAN run. Strict validation and later
ordinary runs completed. A later diagnostic probe also recorded a loading GPU
timeout before the CPU memory-order fix. These GPU failures are distinct from
the guest semaphore stalls. The clean full rebuild has not reproduced either
failure in the checks below; the CPU regressions establish the synchronization
fixes, rather than proving the driver can never time out.

See the recorded results and limits in that directory and the final validation
summary. Existing [replay investigations](combat-replay-results.md) retain the
pre-LAN history and original failed experiments.


### Final main-worktree checks (2026-10-31)

All 129 unit tests passed after the full rebuild. A connected physical gamepad
exposed an existing virtual-controller test isolation gap on the first attempt;
the harness now admits only its virtual VID/PID. Both failed and successful logs
are retained.

| Check | Result |
| --- | --- |
| Three cold LAN starts, 30/120, 60/144 and 120/60 FPS, original versus 32:9 half-resolution FSR/SMAA | All six processes exited 0; 121 observed states matched per pair |
| Full native match after the CPU ordering fix | 8,991 states matched through three rounds |
| Final two-human match, 30 versus 120 FPS, original versus 32:9 half-resolution FSR/SMAA | 6,625 states matched through two rounds; native player 2 victory |
| Final live native input with strict Vulkan and synchronization validation | 601 states matched; input reached both fighters; no validation errors |
| Final ordinary session without a trace file | 120 steps; runtime state hashes and end handshake matched |
| Offline controls: repeated baseline and half-resolution FSR/SMAA | All three exited 0; 601 states matched per case |
| Offline comparison against the earlier separate-worktree replay | All 601 states matched exactly |

The final full match rendered at approximately 29.9 and 118.9 FPS while both
peers simulated approximately 60.0 combat steps per second. Both native fighter
models remained user-controlled with AI disabled in every one of its 6,625
observed states. The two-human policy changes native combat behavior relative to
the earlier solo-opponent tests; it has its own negotiated simulation ID.

Across the two builds containing both startup synchronization fixes, all 16 LAN
game processes and three offline controls completed. No guest startup stall or
GPU timeout was reproduced in those runs. These are finite local checks on one
Linux/NVIDIA machine, not physical-LAN or cross-platform certification.

The final executable SHA-256 is
`f4dd43c4b989595c826bb71176b0c3b46f5746f41d23dedc2c886d7c7641b78b`.
The canonical machine-readable summary is
`build/validation/lan-integration/summary.json`; it links individual reports,
binary hashes, traces, historical failures, and final build/test logs.
