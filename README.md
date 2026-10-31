# Shadow Fight 2 Recomp

Compiler and native runtime source for **Shadow Fight 2, Nintendo Switch update
v131072 (game v1.1.0)**. Users supply their own supported game dump and local keys,
then generate and compile the native game files on their own computer.

**No game files, keys, extracted assets or generated game code are distributed in
this repository.** The locally converted executable contains translated original
game code; its output folder is separate from the public compiler source package.
This project is not affiliated with Nekki or Nintendo.

## Current status

Linux x86-64 renders the animated menu and reaches the English Story/Sensei
introduction. These screens were inspected after removing unrelated Zelda hooks.
The initial Story-dialogue exception now resumes through its guest LSDA landing
pad, and the intro cinematic is decoded and displayed by the host movie path.
Cleanup-only unwinding and rethrow are unsupported. Full combat stability, later
cinematics, and Windows remain unverified.

Rendering has been checked with X11/Xwayland on Mesa lavapipe software Vulkan,
an AMD Radeon 680M (RADV, Mesa 25.2.8), and an NVIDIA RTX 4070 Laptop GPU
(595.99.02). The Radeon uses a driver-owned, coherent guest heap with host-pointer
imports disabled. Selected menu/Story/tutorial tests and 3,000-frame runs reaching
a Kenji sparring fight passed strict Vulkan validation on both hardware GPUs.
Full-game stability and other GPUs remain unverified. The CPU game code executes
natively.

The runtime preserves original scene initialization and Unity internal calls.
Generated main and SDK build IDs must match the loaded files exactly before boot.
Only the tested update is accepted. Optional scaling, FSR/SMAA, filters, caches and
profiling remain; Zelda randomizer/cosmetic/launcher feature chains are removed.

The combined NSP update is applied: extraction takes the update ExeFS and merges
its BKTR RomFS over the base. The official update control metadata reports **1.1.0**
(base: 1.0.0). `v131072` is the content revision, not a display-version calculation.
Earlier notes calling it v2.0.0 were incorrect. The native window now shows
`Shadow Fight 2 (sf2recomp) - 1.1.0`, with version/icon read from the user's NACP
and original control icon. No game icon is shipped in the public source tree.

A guarded CPU buffer-copy change improved one matched software-Vulkan Story test
from 49.38 to 55.87 FPS (+13.1%); see [measurement and limits](docs/performance-notes.md).
This is not a combat or native-GPU performance claim. See the
[platform plan and full-game release gate](docs/portability-and-validation.md)
for the distinction between booting, playable combat and a completed release.

## Build and convert

Use Linux x86-64, Python 3.12+, .NET 8 SDK, Clang/LLD, GNU Make and the native
libraries listed in [runtime dependencies](tools/runtime-dependencies.md).
[Tooling instructions](tools/README.md) document the pinned extraction/analysis
sources, supported package layout and setup. `DOTNET=/path/to/dotnet` selects a
local SDK; no SDK or decryption keys ship in this repository.

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/requirements.txt
# Install the documented native development packages. Conversion and make
# automatically build pinned SDL locally when SDL 3.4+ is unavailable.

python3 tools/convert.py \
  --nsp "/path/to/Shadow Fight 2.nsp" \
  --keys "/path/to/your/prod.keys" \
  --output "$HOME/Games/Shadow Fight 2"
```

SDL headers/libraries, SMAA sources and fallback font binaries are local dependencies, excluded from Git.
`make` and conversion run `tools/setup_runtime.sh --ensure`, which prepares
missing SDL/SMAA/fonts automatically. Existing local or system SDL 3.4+ is reused.
`--check` verifies dependencies without downloads; `--build-sdl` explicitly
rebuilds the pinned SDL source. Setup verifies source checksums.

The `pl:u` shared-font service supplies bundled Noto Sans and Noto Sans CJK
substitutes when Unity requests system fallback fonts. Keep `lib/fonts/` beside
the executable; `make` installs it for development builds and conversion includes
it with OFL licenses. No console firmware fonts are required. Nintendo's private
icon font is not reproduced; that slot uses the standard fallback face.

The supported NSP input is a **combined base + update v131072 bundle containing
its CNMT XML sidecars and tickets**. The supplied development NSP has this layout.
Other package layouts are rejected clearly; separate base/update packages and
arbitrary NSPs are not currently supported. Extraction stages privately and
publishes only after validation succeeds. Keys are local inputs and are not logged.

Conversion builds extraction tools from source as needed, extracts/merges the
update, builds the pinned IL2CPP analysis tool if absent, generates C locally,
compiles the runtime and creates the output folder. Initial source/package setup
needs network access. Subsequent generated chunks are rewritten only when changed.
The supported host build uses up to eight compiler jobs by default (`--jobs N`).

With an existing updated extraction:

```sh
python3 tools/convert.py --game game/extracted/update --output out/shadow-fight-2-linux
```

Output:

```text
Shadow Fight 2/
  sf2                        native executable, generated locally
  sf2recomp.ini              editable runtime settings, controls and save path
  control.nacp / icon.png    official metadata/icon, extracted locally
  game-info.json             official title/version provenance
  org.sf2recomp.ShadowFight2.desktop  native Linux launcher
  extracted/exefs/           matching main, SDK and NPDM
  extracted/romfs/           original game assets, copied locally
  lib/                       local shared SDL library, when used
  licenses/                  notices for included runtime components
  build.json                 source build IDs
  RUN.txt
```

Run `./sf2` from the output folder or any other working directory.
Use `--install-desktop` during conversion/packaging to register that output in
**your user application menu**. Its app ID matches SDL's native window identity;
Wayland desktops use the matching desktop entry for icons where direct icon
setting is unavailable. Moving the output folder requires regenerating/registering
its launcher because it contains absolute local paths. Re-running
conversion preserves an existing output INI and saves. Linux system Vulkan/shaderc
and C/C++ runtime libraries remain required; this is not an AppImage or Windows
package. `--package-only --game DIR` packages an existing local executable only
after checking its main/SDK identities.

Software Vulkan fallback from that folder:

```sh
SDL_VIDEODRIVER=x11 LARECOMP_VK_NO_GMEM=1 \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ./sf2
```

## Runtime configuration and controls

Edit **the INI beside the converted executable**, then restart. Its contents are
read at runtime and are never embedded into generated code. For example:

```ini
[general]
language = en-US

[audio]
volume = 1.0
reverb = true

[video]
fullscreen = false
target_fps = 60
internal_resolution = auto
upscaler = auto
antialiasing = off

[input]
prompts = auto

[experimental]
keyboard_prompts = false

[storage]
save_directory = save

[gameplay]
unlock_all_sparring_characters = true

[controls.keyboard]
a = Space
b = J
ls_up = W

[controls.gamepad]
a = east
b = south
```

`[storage] save_directory` is read on each launch. It accepts an absolute path
or a path relative to the writable data root (normally the converted game folder).
Quote spaces; `~` and environment-variable expressions are literal, not expanded.
`LARECOMP_SAVE_DIR` overrides it for a session. The chosen folder contains
`save_0/userdata/users.xml`, settings and any other mounted slots. To import an
existing `userdata/` backup, copy it under `<save_directory>/save_0/`; keep your
original backup untouched. The SD-card mount, when used, is separate under
`<save_directory>/sdcard/` and does not use save journaling.

Sparring characters are all unlocked by default, independently of campaign saves.
Set `[gameplay] unlock_all_sparring_characters = false` to restore the original
save-based unlocks, then restart. This option changes only the sparring roster in
memory; it does not add unlocks to your save or change Story progression.

The original developer console and debug overlays are optional and disabled by
default. Enable the desired `[developer]` options in the INI, then restart.
Press **F1** or **backtick (`)** to open/close the enabled console, **Enter** to submit,
**Esc** to close, **Up/Down** to browse command history and **PageUp/PageDown** or
the mouse wheel to scroll output. Keyboard and
controller gameplay input is captured while the console is open. **F2** toggles
the original FPS, memory, build-info and model/animation overlays; build/model
details appear in the scenes supported by the game. The console uses desktop
text input instead of the Switch software keyboard.

```ini
[developer]
console = false
trainer = false
shop_cheats = false
overlays = false
overlays_on_start = false
fps = true
memory = true
build_info = true
model_info = true

[gameplay]
unlock_all_videos = false
show_intro = true
ai_enabled = true
show_time_results = false
```

Type `devhelp` for desktop controls and `help` for the game's original commands.
`trainer` lists fight actions: `trainer god`, `trainer bot_god`, `trainer critical`,
`trainer slow`, `trainer pause`, `trainer hud` and `trainer perks` toggle their
corresponding features; `trainer magic`, `trainer combo`, `trainer style`,
`trainer win`, `trainer win_round`, `trainer lose`, `trainer lose_round`,
`trainer reset`, `trainer reset_round` and `trainer step` use the original fight
handlers. Fight actions require an active fight and the game's allowed state.
In the Story shop, `shop show` reveals the original cheat buttons; `shop hide`
hides them. `shop money`, `shop gems`, `shop level` and `shop items` also expose
the implemented shop actions. Currency, items, levels and fight results can be
saved by the game.

`overlay on`, `overlay off` and `overlay toggle` control overlay visibility.
`config ai on/off`, `config intro on/off` and `config timings on/off` change the
internal settings for this session; edit the INI to keep them across launches.
Set the individual `[developer]` options to `false` and restart to disable those
features. Local shop checks are scoped to that panel; online/admin permissions
and the remote developer server are not enabled.

Committed saves persist across launches. Changes are staged as `.pending` files,
then synced/renamed on game Commit; abandoned pending copies are discarded at the
next mount, including nested folders. A copied existing save was loaded, committed
and read by later processes using a configured path. Profile/progression survived;
only normal game-version and regenerated fight timer/seed fields changed.

The supplied INI lists every keyboard/button default. Defaults include K/J/L/I
for A/B/X/Y, WASD for the left stick and arrows for the d-pad. Keyboard values use
SDL key names (`Space`, `Return`, `Keypad 0`); gamepad values name physical
positions (`east`, `south`, `north`, `west`, `ltrigger`, `rtrigger`, etc.). `none`
unbinds an action. Analog gamepad sticks keep their normal axes. Multiple actions
may share a host input. Invalid recognized values warn and preserve the previous
binding; unknown sections/keys are ignored for future additions.

The main menu includes **Exit** to close the native game and return to the
desktop, including in fullscreen. It uses the game's menu navigation and
localized button artwork.

`language = auto` follows the system locale. `SF2_CONFIG=/path/to/custom.ini`
selects another INI; relative overrides are resolved against the launch directory.
Precedence is built-in defaults, then INI, then existing `LARECOMP_*` session
overrides. Saves/caches/logs live beside the executable by default;
`LARECOMP_DATA_ROOT` or `LARECOMP_DATA_HOME` selects another writable state folder.
These overrides do not move the default executable-side INI.

`[input] prompts` accepts `auto`, `keyboard`, `nintendo`, `xbox`, or `playstation`.
Replacing in-game icons/text with keyboard labels is experimental and **off by
default**. Opt in with `[experimental] keyboard_prompts = true`; `prompts = keyboard`
alone does not enable it. With this experiment enabled, labels use current INI
bindings and `prompts = keyboard` forces them even with a controller connected.
When disabled, its hook group bypasses callbacks and runs the original game
instructions; no hint cache or replacement UI is allocated. Each optional site
retains a flag check, and the feature's code remains in the executable.
Direction groups use compact names (`WASD`, `Arrows`); inline dialogue keeps the
game's directional shorthand. Long key names shrink to a bounded width.
Keys inside visible action buttons have matching outline badges: circles for single characters and rounded
rectangles for longer names. Standalone hints stay plain. During the intro, either
mapped A or B skips playback (**K** or **J** with the shipped bindings). The skip
press is consumed so it cannot also activate the menu behind the movie.

NVN presentation enforces the configured frame limit, including when a mailbox
swapchain or disabled vsync would otherwise run uncapped. A positive game
presentation interval multiplies the virtual display period (for example, 2 at
60 Hz gives 30 FPS). `internal_resolution = auto` renders at the window's drawable
pixel size and follows resizing/fullscreen; it does not select a lower-resolution
FSR input. Choose a fractional internal resolution with `upscaler = fsr1` to use
FSR. Higher render resolution improves geometry edges; the game's bitmap artwork
and font atlases retain their original detail.
With the experiment enabled, auto displays mapped keyboard labels when no gamepad
is connected and controller glyphs when one is connected. Input mappings and intro
skip work regardless of this display experiment.

`[video] internal_resolution` accepts `auto`/`match`, `dynamic`, or a fixed
scale from `0.25x` through `4x`. `auto` follows the window's fitted resolution;
`dynamic` lowers render scale when GPU work cannot hold `target_fps`. `upscaler` accepts
`auto`, `bilinear`, or `fsr1`; auto selects bilinear on integrated GPUs and FSR 1
on discrete GPUs. FSR 1 is used only when upscaling. `antialiasing` accepts
`off` or `smaa`. These renderer options take effect on launch. The maximum internal
scale can be limited by the GPU's texture-size cap.

`[video] target_fps` accepts an integer from 30 to 360. It sets Unity's target
frame rate and the emulated display refresh used to pace the game loop. The game
requests 60 FPS in several startup paths, so this setting overrides those requests.
Set the host display to a matching refresh rate separately. Higher targets need
enough CPU and GPU headroom to finish each frame in time.

The developer launcher `tools/run_linux.sh` selects hardware Vulkan when available,
uses the repository INI, and keeps run data under `build/run`. To select this
machine's Radeon explicitly:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json tools/run_linux.sh
```

`tools/run_linux.sh --software` selects lavapipe. On Linux RADV, the default
driver-managed heap shares coherent driver-allocated memory directly with the
CPU, preserving shader global-memory reads and writes without importing anonymous
guest pages. Allocation/mapping failure stops boot instead of silently using
userptr imports. `LARECOMP_VK_MANAGED_HEAP=0` restores the legacy path for developer
diagnostics; that path previously triggered AMD kernel faults on this machine.
Do not carry `LARECOMP_VK_NO_GMEM=1` or a software ICD override into a hardware test.

The opt-in memory check uses the production heap allocator without game files:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json python3 tools/test_gpu_heap.py
```

It requires Clang, Vulkan headers/loader and `glslc`, allocates the full 3.25 GiB
heap, checks CPU-to-compute-shader-to-CPU visibility at the beginning/middle/end,
and repeats with a fresh allocation. The regular unit tests require no GPU.

## Development and verification

```sh
make -j8
python3 -m unittest discover -s tests -v
```

`make gen` regenerates main/SDK from the default extraction after IL2CPP map setup.
See [generator instructions](tools/recompr/README.md). `--check-game DIR` verifies
an executable's original NSO identities without opening a graphical window.

Tests cover corruption-free Vulkan page materialization, exact build identity,
configuration and environment validation, runtime key/gamepad remapping, INI edits
without rebuilding, portable paths, save journaling, extraction rollback and
conversion output contents. Menu captures do not establish combat compatibility.

[Working findings](docs/findings.md) retain historical debugging context; removed
experimental flags described in old entries are not current features.

## Licensing

Project additions use the root [MIT license](LICENSE). Imported runtime/generator
code retains its original notices. See [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES)
and the centralized texts in [licenses/](licenses/). Downloaded dependency trees
keep upstream notices locally but are excluded from Git. Game data and generated game-derived output are
not covered by those software licenses. Users must have the rights required for
their local inputs and use; owning a copy alone is not a worldwide legal clearance.
