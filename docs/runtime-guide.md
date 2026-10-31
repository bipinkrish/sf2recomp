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
