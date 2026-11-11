# Combat replay investigation

The replay harness tests whether the same logical fighter inputs produce the same
observed combat state in separate game processes. Its native initialization and
state observer are also used by [opt-in LAN Sparring](lan-sparring.md). Replay
diagnostics add no rollback or gameplay clock replacement. PvP wall margins use the original 16:9
combat width independently of display aspect. Seeded test mode
adds controlled native initialization and scoped random streams; it is opt-in.
It supports the checked SF2 update v131072 build only.

## Build and run

Regenerate main after adding the checked replay hook sites, then rebuild:

```sh
python3 tools/recompr/gen.py main game/extracted/update/exefs/main \
  0x7000000000 game/gen/main --symbols game/il2cpp/sym_main.txt
make -j8
```

Run the matrix from the checkout containing that build:

```sh
python3 tools/combat_replay.py matrix \
  --output build/validation/combat-replay/example --seed 12345 \
  --save-fixture /absolute/path/to/a/save-directory \
  --icd /usr/share/vulkan/icd.d/nvidia_icd.json
```

The output directory must be new. The runner makes a private copy of the fixture
for every process; omitting the fixture starts each process with an empty save.
It uses hidden 960x540 windows, disables human input and game AI, keeps original
game networking blocked, and feeds both fighters a 600-step logical input tape.
The existing menu-navigation script is used only to reach Sparring. At
`PvpScene.StartFight`, the diagnostic fixture selects roster entries 17 and 18
(Wraith and Smother) and arena entry 13 (Statue) from the game's own objects.
The setup trace records their template IDs and resolved equipment. This removes
the menu script's Random opponent/arena choices without changing combat RNG.

Cases are two identical 60 FPS controls; 30, 120 and 144 FPS targets; a 60 FPS
half-resolution bilinear case, a half-resolution FSR/SMAA case; and 60 FPS
16:10, 21:9 and 32:9 cases. `--cases` selects a comma-separated subset and must include `baseline-a`. A repeatability conclusion
requires `baseline-b` too. Use `--ticks` to change the observation length and
`--timeout` for the per-process wall-clock limit. Use `--scenario walls --ticks 2000`
for a second tape that pursues to the left, attacks, traverses to the right, and
attacks again. The default `exchange` tape is unchanged. `--scenario edges --ticks 1000`
sends P1 left and P2 right for 800 steps before attacking, to test both boundaries
and the zoom-out limit without the fighters blocking one another.

GPU/display access is required for real runs. Set `SDL_VIDEODRIVER=x11` on an X11
desktop if needed. A frame-rate target does not guarantee that rate is achieved:
the comparison report includes measured presentation and combat-step rates.
Snapshot formatting and file I/O add overhead, which can itself expose timing
sensitivity. Each case has its own shader/cache data; loading stalls can differ.

Each case produces `sf2recomp.ini`, `environment.json`, `trace.jsonl`, and game logs.
On Linux the runtime normally redirects diagnostics to `logs/shadow-fight-2.log` in the
case's data directory; `runtime.log` also captures inherited process output.
The top-level `report.json` records the executable/input hashes, fixture hashes,
exit status, renderer execution mode, state comparisons, and first differing
field paths.
`--render-thread 0` selects the existing synchronous renderer for a separate
diagnostic batch; the default is 1. Matrix exit 0
requires the repeat control and every selected comparison to match; exit 1
reports differences or incomplete evidence; CLI/argument failures return 2.

## Tape and trace contract

Create a reusable tape separately:

```sh
python3 tools/combat_replay.py scenario /tmp/combat-inputs.txt --ticks 600
```

Its text format is:

```text
SF2_REPLAY 1
# tick  player  control-quadrant  down
0 1 3 1
0 2 7 1
75 1 3 0
75 2 7 0
85 1 9 1
89 1 9 0
```

Ticks start at zero at the first active PvP combat substep. Player is 1 or 2;
quadrant is the game's logical 0..14 value (directions 1..8, punch 9, kick 10,
missile 11, magic 12, raid charge 13, super 14); down is 1 or 0. Events must be
ordered by tick. Their order within a tick is preserved. All events must be before
the requested stop tick. This records the model-level action after device/team
routing; it does not record physical keyboard/controller events or menus.

Runtime environment options:

| Variable | Meaning |
| --- | --- |
| `SF2_REPLAY_TRACE` | Required new JSONL output path; enables diagnostic hooks. |
| `SF2_REPLAY_INPUT` | Replay an existing tape; suppress other logical press/release calls on the two fighters after capture begins. |
| `SF2_REPLAY_RECORD` | Record live logical fighter commands into a new tape; mutually exclusive with INPUT. |
| `SF2_REPLAY_TICKS` | Positive number of combat substeps, default 600, maximum 1,000,000. |
| `SF2_REPLAY_SEED` | Optional uint32 seed; enables fixed-step native initialization and scoped random streams. Omit for original observation mode. |
| `SF2_REPLAY_MATCH` | Optional zero-based character,character,arena catalog indices, e.g. `17,18,13`; Random entries are rejected. |

Without INPUT or RECORD, TRACE observes the fight. Existing output files are never
overwritten. Recording fails if an input arrives inside a combat substep, since a
tick-only tape could not preserve that intra-step ordering. A recording starts
after the initial stance, so replay also requires equivalent initial controller
state; the initial snapshot detects differences. Use a fresh process for each
run. Seeded mode instead establishes the native round/stance sequence described
below.
The automatic matrix uses one human-controlled and one AI-disabled fighter slot,
injecting both through their original model methods. It does not test remote HID
routing or two physical controllers.

The before/after hooks bracket each unpaused inner step of `Fight.Draw`, including
wall updates, combat, and camera updates, before the original frame increment.
They count both steps of the game's reduced-FPS path separately. The first state
is tick 0 before tape injection, followed by states 1..N after each step. The
fixed-update scheduler remains original; the optional startup policy is described
below. Injected input, health getters and explicit transition calls save and restore
the caller's full CPU state. Wrapped native calls retain their original return
values and callee register behavior.

Float values are stored as integer bit patterns to compare exactly. State includes
round/stage counters, fighter health via the original getter, equipment, selected
model flags, animation identity/frames/buffers/walls, controller history, model
node current/previous positions, and physics frame counters. Runtime pointers,
obscured health storage bytes, timestamps and presentation counts are excluded
from state equality. The original logical `Fight.frame` counter is compared,
even though stored with diagnostics; it can affect later simulation. Presentation
and elapsed-time diagnostics remain available.

## Combat geometry and aspect ratio

A checked hook in native `Render.Init` supplies the original 16:9 reference width
for PvP before native code stores it and derives the minimum scale. Camera scale,
location center and facing-sensitive wall margins then consistently use this
reference. A width-only override in `Fight.UpdateWalls` was insufficient because
location width/center still depended on display-driven camera scale.

The reference uses original integer pixel width (16/9 times native pixel height)
divided by the renderer's pixel-to-location scale, preserving native division
order. All subsequent geometry updates and clamps remain native. Actual display
and backing-buffer dimensions still use the selected aspect; this preserves
original PvP framing within that viewport instead of enlarging the combat arena
or changing zoom to fill the wider display.

The fix applies to custom-aspect ordinary Sparring independently of replay/seed
options. Other fight modes and original aspect preserve native behavior. Trace
coverage v3 includes the six reference-width/scale/center fields, as well as both
fighters' margins and nodes; they remain part of exact comparison. Traces identify
this behavior as `combat_geometry: pvp-framing-16:9-v2`, and comparisons with older
geometry policies are rejected.

## Controlled initialization and randomness

Supply `--seed 12345` to the matrix (or `SF2_REPLAY_SEED=12345` directly). This is
an experimental replay policy, not a production gameplay change. Without it,
the initialization and RNG hooks execute their original instructions.

Combat draws wait until `ViewerFight.Round` has been initialized by the original
UI loading path. At that boundary the harness checks that the logical fight
frame and round number are still zero. It calls the original `NextRound` and
`StartStance` methods, runs 240 unpaused inner combat steps with no tape inputs,
then calls original `PlayFight` before the next step. Intro callbacks cannot
independently restart the round/stance/play sequence. The warmup counts combat
steps, including both inner steps of reduced-FPS mode, rather than presentations.
The native model/controller/animation/node updates actually run; their counters
and float bits remain in the comparison. No state fields are zeroed or excluded
to manufacture equality. Live model input is suppressed from selection onward
in controlled playback, including menu-button spillover during the intro.

Random control is scoped to native fight construction, simulation and the
explicit transitions on the guest update thread. Outside those scopes, the
original sources run. Nekki's original range/chance methods retain their native
arithmetic and two-draw precision; their underlying LCG values come from the
configured seed and outer random-call return address. Unity float/int `Range`
uses separate seeded LCG streams by caller and result type. Integer ranges are
exclusive at the upper bound; floats use an inclusive 24-bit fraction. Native
Nekki seed writes in these scopes use the configured seed instead of UTC-derived
values. Native reseeds do not restart the independent call-site streams.

This deliberately defines a new replay random policy; it does not reproduce a
past unseeded match or Unity's original random algorithm. A visual call site has
its own stream and cannot consume a draw from a combat call site. Streams are
keyed by code addresses, never heap pointers, and every snapshot compares their
sorted keys, states and draw counts. Trace coverage is `observed-combat-fields-v3`
(v1 lacked random-stream state; v2 lacked the framing fields). This also exposes extra random work between
runs. Combat random calls reachable through other entry points or future game
builds require another audit; this is not a blanket engine-wide RNG replacement.

The failed first freeze experiment is retained separately: holding every draw
prevented the native intro from completing, so it did not produce combat evidence.
The fixed-step native transition policy replaces that approach.

## Interpret results

```sh
python3 tools/combat_replay.py compare \
  build/validation/combat-replay/example/baseline-a/trace.jsonl \
  build/validation/combat-replay/example/fps-120/trace.jsonl
```

The comparator rejects empty/truncated traces, missing or repeated ticks,
incompatible setups, differing applied input sequences, and missing completion
markers. It reports the first mismatching tick and named field paths rather than
comparing screenshots or process memory addresses. It validates and compares
large node traces incrementally instead of retaining two complete matches in RAM.

If the identical-settings repeat control diverges, the experiment has established
non-repeatability at fixed settings. Cross-setting differences then cannot be
attributed specifically to FPS or graphics settings. The header distinguishes `original-unmodified` observation from the controlled
`scoped-callsite-lcg-v2` experiment (mixed per-scope seeds; historical traces use v1). Different seeds, RNG policies and initialization
policies are incompatible comparisons.

Matching traces establish equality only for the recorded fields and tested
scenario. This is not a complete save/restore serializer: all pending events,
perk internals, engine scheduler state and every physics dependency are not
captured. Node positions include the model's full node list, so a differing node
alone needs investigation before claiming a different hit outcome. Longer fights,
additional attacks, pauses, slow motion, rounds and cross-platform runs are needed
before considering LAN synchronization reliable.
