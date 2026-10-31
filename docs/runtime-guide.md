# Settings and controls

[Back to the README](../README.md#settings-and-controls).

## Configuration file

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
player_setup = solo

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

## Offline networking

Networking is disabled by default, including when an older INI has no network
section. To keep the game offline, use:

```ini
[network]
enabled = false
```

The translated game reports no internet connection. Socket creation, connections,
sends, DNS lookups, external web links and the CUDLR network listener are blocked.
The runtime also denies network IPC services and host network overrides, so adding
a transport implementation later does not enable it by default. Local assets,
cinematics and save files remain available.

Setting `enabled = true` and restarting restores the original network paths; it
does not implement missing online services. This option covers game networking;
dependency downloads during setup/build and local desktop display/audio IPC are
separate.

## Saves

`[storage] save_directory` is read on each launch. It accepts an absolute path
or a path relative to the writable data root (normally the converted game folder).
Quote spaces; `~` and environment-variable expressions are literal, not expanded.
`LARECOMP_SAVE_DIR` overrides it for a session. The chosen folder contains
`save_0/userdata/users.xml`, settings and any other mounted slots. To import an
existing `userdata/` backup, copy it under `<save_directory>/save_0/`; keep your
original backup untouched. The SD-card mount, when used, is separate under
`<save_directory>/sdcard/` and does not use save journaling.

Committed saves persist across launches. Changes are staged as `.pending` files,
then synced/renamed on game Commit; abandoned pending copies are discarded at the
next mount, including nested folders. A copied existing save was loaded, committed
and read by later processes using a configured path. Profile/progression survived;
only normal game-version and regenerated fight timer/seed fields changed.

## Sparring roster

Sparring characters are all unlocked by default, independently of campaign saves.
Set `[gameplay] unlock_all_sparring_characters = false` to restore the original
save-based unlocks, then restart. This option changes only the sparring roster in
memory; it does not add unlocks to your save or change Story progression.

## Developer console and overlays

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

## Player assignment

Set `[input] player_setup` in the INI beside the executable, then restart:

| Value | P1 | P2 |
| --- | --- | --- |
| `solo` (default) | Keyboard and first controller | Second controller |
| `keyboard_vs_controller` | Keyboard | First controller |
| `two_controllers` | First controller | Second controller |

For keyboard P1 versus one controller P2 in Sparring, use:

```ini
[input]
player_setup = keyboard_vs_controller
```

`two_controllers` disables keyboard gameplay input; developer shortcuts still work.
Controllers fill available slots in connection order. Disconnecting one clears its
input and vibration without moving the other player; a new connection fills a free
slot. In `keyboard_vs_controller`, P1 is reserved for the keyboard and additional
controllers are ignored while P2 has a controller. Controllers must be recognized
as SDL gamepads; custom mappings can be supplied in `gamecontrollerdb.txt`.
Missing or invalid settings preserve the default or previously loaded value.
Button remapping is independent of player assignment.

## Keyboard and controller bindings

The [supplied INI](../sf2recomp.ini) lists every keyboard/button default. Defaults include K/J/L/I
for A/B/X/Y, WASD for the left stick and arrows for the d-pad. Keyboard values use
SDL key names (`Space`, `Return`, `Keypad 0`); gamepad values name physical
positions (`east`, `south`, `north`, `west`, `ltrigger`, `rtrigger`, etc.). `none`
unbinds an action. Analog gamepad sticks keep their normal axes. Multiple actions
may share a host input. Invalid recognized values warn and preserve the previous
binding; unknown sections/keys are ignored for future additions.

The main menu includes **Exit** to close the native game and return to the
desktop, including in fullscreen. It uses the game's menu navigation and
localized button artwork.

## Language and file locations

`language = auto` follows the system locale. `SF2_CONFIG=/path/to/custom.ini`
selects another INI; relative overrides are resolved against the launch directory.
Precedence is built-in defaults, then INI, then existing `LARECOMP_*` session
overrides. Saves/caches/logs live beside the executable by default;
`LARECOMP_DATA_ROOT` or `LARECOMP_DATA_HOME` selects another writable state folder.
These overrides do not move the default executable-side INI.

## Experimental keyboard prompts

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

With the experiment enabled, auto displays mapped keyboard labels when no gamepad
is connected and controller glyphs when one is connected. Input mappings and intro
skip work regardless of this display experiment.

## Graphics and frame rate

Optional aspect ratios are experimental and disabled by default. Set the ratio
under `[video]` in `sf2recomp.ini`, then restart:

```ini
[video]
aspect_ratio = 21:9
```

| Setting | Native screen before resolution scaling |
| --- | --- |
| `original` or `16:9` (default) | 1920×1080 |
| `16:10` | 1728×1080 |
| `21:9` | 2520×1080 |
| `32:9` | 3840×1080 |

This changes Unity's native screen modes, camera/layout dimensions and
color/depth buffers together. Presentation scales uniformly to fit the selected
ratio into the window; it does not stretch the final image. For example, 1920×1200
fills the window in 16:10 mode, 5120×1440 fills it in 32:9 mode, and 3440×1440
has 40-pixel bars on each side in exact 21:9 mode. Movies retain their original
proportions with black bars. Invalid settings keep the previous valid value.

Some menus and artwork may retain their original width. Wider framing can
expose background edges: the garden stage showed sky gaps near its top corners
at 21:9. In 32:9, stage scenery uses uniform 2× cover scaling to fill the wider
view, cropping background detail vertically. This applies only to backdrop and
static scenery layers. 21:9 and 32:9 keep the original 16:9 fight-camera zoom
reference so fighters' feet and ground markers remain in view. This also avoids
zooming gameplay effects vertically; HUD proportions remain unchanged. Scenery
that follows fight zoom is anchored to the original floor so the black ground
stays beneath the fighters.
The night mountain/lake stage's former black side gaps were retested at
5120×1440 during movement and attacks and no longer appeared. Sparring was
tested in all three modes with zero Vulkan validation errors. Normal Story
Tournament combat (Shadow versus Thorn) was also tested with movement and
attacks at 16:10, 21:9 and 32:9, with zero Vulkan validation errors. Coverage of
every stage and campaign screen is unverified.

NVN presentation enforces the configured frame limit, including when a mailbox
swapchain or disabled vsync would otherwise run uncapped. A positive game
presentation interval multiplies the virtual display period (for example, 2 at
60 Hz gives 30 FPS). `internal_resolution = auto` renders at the window's drawable
pixel size and follows resizing/fullscreen; it does not select a lower-resolution
FSR input. Choose a fractional internal resolution with `upscaler = fsr1` to use
FSR. Higher render resolution improves geometry edges; the game's bitmap artwork
and font atlases retain their original detail.
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

## Software Vulkan fallback

From the converted output folder:

```sh
SDL_VIDEODRIVER=x11 LARECOMP_VK_NO_GMEM=1 \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ./sf2
```

This selects Mesa lavapipe for diagnostics. Use hardware Vulkan for normal
hardware tests; see [GPU diagnostics](development.md#local-launcher-and-gpu-diagnostics).
