# INI configuration and controls

[Back to the README](../README.md#settings-and-controls).

## Change settings in the game

Open **Recomp Settings** from the main menu. Choose a category and edit the
values. Select **Save to INI** beside **Back**. The game applies supported
changes immediately and labels startup-bound settings **Restart required**.

Master volume, reverb, fullscreen, internal resolution, upscaler, and
anti-aliasing apply on save. Fight AI, time results, movie unlocks, the fight
trainer, and shop cheats use the new values when you next use them. Language,
aspect ratio, target FPS, bindings, and save profile settings require a restart.

The menu writes to the same INI the game loaded. If you set `SF2_CONFIG`, the
menu saves to that file. Environment overrides still take precedence at launch.

## Configuration file

Use **`sf2recomp.ini` beside `sf2.exe` or `sf2`** in your converted game folder.
On Android, it is in `Android/data/org.sf2recomp.app/files/sf2recomp.ini`.

1. Close the game.
2. Make a backup of your INI, then open it in a plain-text editor.
3. Change the values you want, save the file, and start the game again.

You do not need to convert or rebuild the game after editing. Converting into
the same output folder preserves an existing INI and your saves.
The [supplied INI](../sf2recomp.ini) lists the shipped settings and comments.

### Common changes

These are the values in the supplied file, which can differ from the runtime's
built-in fallback defaults.

| Setting | Shipped value | What to change |
| --- | --- | --- |
| `[general] language` | `en-US` | Use `auto` for your system language, or a locale such as `fr`, `de`, or `ja`. |
| `[audio] volume` | `1.0` | Choose `0.0` to `1.0`; `0.0` mutes the game. |
| `[video] fullscreen` | `true` | Set `false` for a resizable window. |
| `[video] target_fps` | `120` | Choose `30` to `360`; match your display's refresh rate separately. |
| `[video] internal_resolution` | `auto` | Use `dynamic` to adapt to GPU load, or a fixed scale such as `0.75x`. |
| `[video] aspect_ratio` | `original` | Use `native` for your window/device ratio; `16:10`, `21:9`, and `32:9` are experimental. |
| `[input] player_setup` | `keyboard_vs_controller` | Use `solo` for keyboard and the first controller on P1, or `two_controllers` for separate gamepads. |
| `[gameplay] show_intro` | `true` | Set `false` to skip the opening video. |
| `[storage] use_completed_save` | `false` | Set `true` to use the bundled completed campaign in a separate profile. |

Keep each key under its named `[section]`. Comments start with `#` or `;`.
Use `true` or `false` for switches. Unknown sections and keys are ignored.

The example below changes some shipped settings: it uses a window, targets
60 FPS, puts the keyboard and first controller on P1, and maps A to Space.

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

[storage]
save_directory = save
use_completed_save = false

[gameplay]
unlock_all_sparring_characters = true
allow_sparring_lan_multiplayer = true

[controls.keyboard]
a = Space
b = J
ls_up = W

[controls.gamepad]
a = east
b = south
```

### Find a setting

| Change | Reference |
| --- | --- |
| Resolution, frame rate, aspect ratio, FSR, or anti-aliasing | [Graphics and frame rate](#graphics-and-frame-rate) |
| Key bindings, controllers, or mouse menus | [Keyboard and controller bindings](#keyboard-and-controller-bindings) |
| Which controller belongs to each player | [Player assignment](#player-assignment) |
| Save folder, imported saves, or completed profile | [Saves](#saves) |
| Unlocked fighters, Shadow, or Titan | [Sparring roster](#sparring-roster) |
| Console, trainer, or overlays | [Developer console and overlays](#developer-console-and-overlays) |
| Language, alternate INI, or writable data folder | [Language and file locations](#language-and-file-locations) |

## Offline networking

The original publisher/server networking is disabled by default, including when
an older INI has no network section. To keep those paths disabled, use:

```ini
[network]
enabled = false
```

The translated game reports no internet connection. Socket creation, connections,
sends, DNS lookups, external web links and the CUDLR network listener are blocked.
The runtime also denies network IPC services and host network overrides. Local
assets, cinematics and save files remain available. The native LAN Sparring
transport is separate: it opens local sockets only after choosing Host or Join
and is controlled by `[gameplay] allow_sparring_lan_multiplayer`.

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

Shadow and Titan are also available by default. Control them separately in the
INI, then restart:

```ini
[gameplay]
enable_shadow_in_sparring = true
enable_titan_in_sparring = true
```

Shadow uses the equipment currently equipped in the active campaign profile,
including armor, helmet, ranged weapon and magic. Without a started campaign,
he uses the unequipped starter body and fists, with no ranged weapon or magic.
His loadout is refreshed for each fight; Sparring does not equip or modify items
in the campaign. Shadow and Titan do not use the weapon selection carousel.

Titan uses his giant sword, extendable arm on the ranged input, and Mind Throw
on the magic input. Headbutts, stomps, sword attacks and throws use the normal
attack buttons and directional combinations. His shield is automatic; he has no
separate active boss-power button. His versus screen has no ability panel.
His real magic uses the normal magic control on desktop,
gamepad and mobile.

Sparring remains first to two wins. Titan uses the same combined loadout in every
round, choosing the strongest values from his three campaign phases: 30% sword
lifesteal, 22% harpoon lifesteal, 50% magic Enfeeble and Weakness, 70% head-hit
magic recharge, helmet regeneration with a 1.375 chance factor, a three-second
shield cooldown, 15% Enlightenment restoring 15% health, and 50% Ricochet.
These are configured perk chances; enchantment resistance and native trigger
conditions still apply. The campaign values and selected values are tabulated
beside the Titan template in `runtime/hle/sparring_roster_hooks.inc`.

The roster keeps its original portrait sizes and 11 columns. Three rows remain
visible. Move down from the third row to bring the extra row into view, and move
back up to reveal the first row. Keyboard and gamepad navigation skip unused
positions in the final row. Mouse and touch select only visible cells.
The mouse wheel and a vertical swipe over the roster also reveal or hide the
extra row. A swipe does not confirm the fighter underneath your finger.

Both LAN players must use the same values for these roster options. The LAN
handshake rejects mismatched rosters before selection indices are exchanged.
Shadow's campaign loadout is local to each installation. Use matching equipped
gear on both machines for LAN matches involving Shadow.

## Boss refights and the completed save

Select a defeated boss on the Story map, then select **Refight** or press the
mapped **X** button. This works for Lynx, Hermit, Butcher, Wasp, Widow, Shogun and
Titan, including the six boss encounters after Gates of Shadows. Gates of
Shadows also has Refight, which starts its full boss sequence. The completed tag
remains visible. The movie button keeps its **A** shortcut. Refight starts the boss's final fight, skipping the bodyguards.
Winning, losing or quitting a refight grants no money, experience, items,
achievements or campaign progress, and does not save.

Android APKs and native app packages include the completed campaign snapshot.
To load it, set this option in `sf2recomp.ini` and restart:

```ini
[storage]
use_completed_save = true
```

The default is `false`. The first enabled launch copies the bundled snapshot to
`completed/save_0` inside your configured `save_directory`. Later launches and
package updates preserve that separate profile, including equipment changes.
Your normal save stays in `save_0`. Set the option back to `false` and restart to
return to it. The bundled template stays in `profiles/completed` beside the app's
other resources. Boss refights leave the selected profile unchanged.

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

The supplied INI lists every keyboard/button default. Defaults include K/J/L/I
for A/B/X/Y, WASD for the left stick and arrows for the d-pad. Keyboard values use
SDL key names (`Space`, `Return`, `Keypad 0`); gamepad values name physical
positions (`east`, `south`, `north`, `west`, `ltrigger`, `rtrigger`, etc.). `none`
unbinds an action. Analog gamepad sticks keep their normal axes. Multiple actions
may share a host input. Invalid recognized values warn and preserve the previous
binding; unknown sections/keys are ignored for future additions.

Desktop menu shortcuts also accept **Enter** (or keypad Enter) to confirm and
**Escape** to go back. In Sparring selection, Escape leaves for the main menu;
the configured B key still undoes equipment/Ready selection. During combat, unassigned Escape opens the native pause
menu; the default Pause/Plus key is now **Tab**. **Space** skips video cutscenes
while a video is playing. These shortcuts are fallbacks: if a key is assigned to
any keyboard action (including a stick direction), its configured action wins.
An existing INI with `plus = Return` therefore keeps Enter as Pause; use
`plus = Tab` to enable Enter as the menu-confirm shortcut. The configured A/B
keys keep working alongside the shortcuts, and binding capture and the developer
console retain their own Enter/Escape handling.

The main menu includes **Exit** to close the native game and return to the
desktop, including in fullscreen. It uses the game's menu navigation and
localized button artwork.

Menus also accept **mouse hover and left-click**, using the game's own pointer
handlers and hit testing. Sliders and scroll views with existing pointer handlers
accept dragging and the mouse wheel. The pointer maps to the displayed game area
after resizing, including fullscreen, HiDPI and custom aspect ratios; black bars
are outside the clickable area. Keyboard/controller navigation clears mouse hover
until the mouse is used again. Mouse input does not map to combat buttons. Sparring character and arena cells
also accept a left-click to select and confirm the active player's choice.
In LAN, each computer's clicks control only its own player and are synchronized
with the peer. Equipment confirmation continues through the native controls.

For input diagnostics, `LARECOMP_MOUSE_TRACE=1` logs SDL pointer events and their
mapped Unity coordinates. Scripted runs can use `LARECOMP_HOST_INPUT` entries such
as `180:mouse:at/640/415,220:mouse:click`; coordinates are window points. `down` and
`up` can replace `click` to test dragging.

## Language and file locations

`language = auto` follows the system locale. `SF2_CONFIG=/path/to/custom.ini`
selects another INI; relative overrides are resolved against the launch directory.
Precedence is built-in defaults, then INI, then existing `LARECOMP_*` session
overrides. Saves/caches/logs live beside the executable by default;
`LARECOMP_DATA_ROOT` or `LARECOMP_DATA_HOME` selects another writable state folder.
These overrides do not move the default executable-side INI.

## Controller prompts and intro playback

`[input] prompts` accepts `auto`, `nintendo`, `xbox`, or `playstation`.
Menu footer strips are hidden on desktop and Android; dialogs and combat prompts
retain the original game artwork.

During the intro, either mapped A or B skips playback, **K** or **J** with the
shipped bindings. The skip press is consumed so it cannot also activate the menu
behind the movie. Mouse clicks also skip playback and are consumed through release. On Android, tapping
the video skips it; that finger remains captured until release.

The intro decoder starts before graphics and engine initialization finish. Module
loading runs off the window thread so it can present the opening frames sooner;
the game reuses that playback, and a skip during boot does not replay the intro.
Startup data loads while the intro plays. On GPUs that import the guest heap,
the runtime also prepares that memory in a background worker from the first
movie presentation, reducing the first-menu pause after skipping. Skipping very
early can still require time for startup data parsing to finish.

Android decodes the bundled H.264/AAC movies in-process using MediaCodec and
MediaExtractor. Video planes are copied using their reported strides into the
existing Vulkan movie overlay; audio uses the shared SDL playback clock. Frame
zero must reach the window before audio starts, and pre-roll queues are bounded.
Desktop builds retain their bundled FFmpeg decoder. On Android, host completion
also calls the original managed completion handler, including on skip, so the
game can leave the video screen immediately. Skipping does not mark a video as
watched to the end.

Android stores `sf2recomp.ini`, `save/` and `logs/shadow-fight-2.log` in
`Android/data/org.sf2recomp.app/files`. The bundled game stays in private app
storage; editing the INI does not require re-extracting it.

Sparring bosses have a separate touch power button with centered spiral
artwork, Magic's size and palette, and native cooldown/ready state. It continues
the action buttons' zigzag layout. Ordinary fighters do not show it. On
desktop, the default combination is **1 + 3** (ZL + ZR); remapped trigger bindings
apply as usual.

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
| `native` (Android default) | Screen aspect ratio at 1080 lines, rounded to an even width |

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
`off` or `smaa`. You can apply these renderer options from **Recomp Settings**. The maximum internal
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

## Native LAN Sparring

Sparring offers Normal or LAN Multiplayer, followed by Host/Join and automatic
local host discovery. No Python or launcher is required in the converted game.
The default-on `[gameplay] allow_sparring_lan_multiplayer` option controls this
entry; false restores the direct Normal path. See [LAN Sparring](lan-sparring.md).
