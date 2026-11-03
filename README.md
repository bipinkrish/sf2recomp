# SF2Recomp

Play as mad Titan himself against a friend over LAN, or face him again in a boss
refight, with support for 4K, 120 FPS and ultrawide displays on your choosen device.

A static recompilation of **Shadow Fight 2 for Nintendo Switch** into a native application with
offline Sparring PvP, LAN Sparring, mouse menus, remappable controls and graphics
settings.

**You supply the game files. No game code, assets or keys are included.**

## Status

**Supported game input:** a combined base + **v1.1.0 / update v131072** NSP with
its CNMT XML sidecars and tickets. Separate base/update NSPs, NSZ and other
versions are not supported yet.

| Platform | Status |
| --- | --- |
| Linux x86-64 | Tested desktop development target. |
| Windows x86-64 | Experimental; native gameplay validation pending. |
| macOS Apple Silicon | Experimental; native gameplay and GPU validation pending. |
| Android arm64 / x86-64 | Development builds |

## Download and play

1. Download the SF2Recomp tool for your computer from
   [Releases](https://github.com/bipinkrish/sf2recomp/releases).
2. Extract the whole archive into a writable folder.
3. Open `sf2recomp.cmd` on Windows, `SF2Recomp.command` on macOS, or
   `sf2recomp` on Linux.
4. Enter your NSP and `prod.keys` paths, then choose an output folder.
5. Wait for conversion, then run `sf2.exe` or `sf2` from the output folder.

The desktop release bundles its conversion tools and compiler. You need a
compatible Vulkan 1.3 GPU and graphics driver. Windows and macOS remain
experimental; successful CI checks do not establish gameplay compatibility.

Already extracted updated files work without
keys through `--game`. Release packages are built by the desktop release workflow. If no downloadable
package is available yet, use the [source build guide](docs/source-guide.md#requirements).

Command-line conversion:

```sh
./sf2recomp --nsp "/path/game.nsp" --keys "/path/prod.keys" --output "/path/Shadow Fight 2" --play
```

On Windows, use `sf2recomp.cmd` in place of `./sf2recomp`.

## Android

Use the same desktop tool to build a private APK for your phone. Select Android
at the prompt, or run:

```sh
./sf2recomp --android --nsp "/path/game.nsp" --keys "/path/prod.keys" --output "/path/APK" --accept-android-licenses
```

On first use, the extension downloads Java, SDK/NDK and
build dependencies into the tool folder. It needs several GB and an internet
connection. Later builds reuse these files. Android Studio is not required.

## Features

Keyboard defaults: **WASD** to move, **K/J/L/I** for A/B/X/Y, and
**Return/Backspace** for Plus/Minus. Edit `sf2recomp.ini` in the converted game
folder for settings. Saves live under `save/` by default.

[Settings and controls guide](docs/runtime-guide.md) ·
[LAN guide](docs/lan-sparring.md)

The recompilation adds desktop features and graphics options beyond the Switch
release, and exposes some of the game's original developer tools. Settings below
live in **`sf2recomp.ini` beside the converted executable** and take effect after
restarting; no rebuild is needed.

Screenshots are from native Linux and Android builds. Click an image for full
resolution.

### Ultrawide and graphics

- **Aspect ratios (experimental)**. `[video] aspect_ratio` accepts `16:10`, `21:9` or `32:9`, with native screen sizes of 1728×1080, 2520×1080 and 3840×1080 before uniform scaling. Restart after changing; `original` / `16:9` is the default. In 32:9, scenery is enlarged uniformly to cover the sides, cropping some background detail vertically. Fighter and HUD proportions are preserved. Movies keep their original proportions.

- **Native display resolution**. `[video] internal_resolution = auto` or `match` renders at the window's fitted drawable pixel size, following window resizing and fullscreen. Higher resolutions improve geometry edges; bitmap artwork and font atlases retain their original detail.

- **Resolution scaling**. Fixed internal-resolution presets of `0.25x`, `0.50x`, `0.75x`, `1x`, `2x`, `3x` and `4x` let you trade rendering detail for performance, within the GPU's texture-size limit.

- **Dynamic resolution**. `[video] internal_resolution = dynamic` adjusts render scale in response to GPU load to work toward the configured frame-rate target.

- **AMD FSR 1 upscaling**. `[video] upscaler = fsr1` enables spatial upscaling and sharpening when the rendered image is smaller than its window area. Use a lower internal resolution to activate it; `auto` selects bilinear on integrated GPUs and FSR 1 on discrete GPUs.

- **SMAA anti-aliasing**. `[video] antialiasing = smaa` enables optional post-process edge smoothing. Disabled by default.

- **Configurable frame rate**. `[video] target_fps` accepts **30–360**, including targets above 60 FPS, and sets the game's target frame rate and presentation pacing. Achievable performance depends on hardware; set the host display refresh rate separately.

- **Desktop window modes**. A resizable window or fullscreen, selected with `[video] fullscreen`.

[![Wraith versus Aquaris on the night lake stage, filling the 32:9 view](docs/screenshots/ultrawide-night.png)](docs/screenshots/ultrawide-night.png)

*32:9 gameplay at 5120×1440, with scenery cover scaling.*

### Desktop controls and menus

- **Remappable keyboard and controllers**. `[controls.keyboard]` and `[controls.gamepad]` map desktop inputs to Switch actions, including unbinding actions and sharing inputs.

- **Mouse menus**. Hover and left-click use the game's existing menu buttons. Pointer coordinates follow window resizing, black bars, HiDPI displays and configured aspect ratios; existing sliders and scroll views also receive mouse drag and wheel input.

- **Menu layout**. The bottom controller-hint strips are hidden on desktop and Android. Centered dialog prompts, combat hints and the gameplay HUD remain visible.

- **Desktop Exit button**. The main menu's localized **Exit** option closes the game and returns to the desktop, including in fullscreen.

- **Desktop configuration**. The INI also provides language selection (including system-locale detection), master volume and reverb controls. Alternate configuration and writable data locations can be selected at launch.

<p>
  <a href="docs/screenshots/main-menu.png"><img src="docs/screenshots/main-menu.png" width="49%" alt="Desktop main menu without the bottom controller-hint strip and with an Exit option"></a>
  <a href="docs/screenshots/mouse-settings.png"><img src="docs/screenshots/mouse-settings.png" width="49%" alt="The current desktop Settings menu with the Sound slider selected by mouse and no bottom controller hints"></a>
</p>

*Desktop main menu and Exit option · Mouse-operated settings.*

### Sparring roster

- **Unlocked Sparring roster**. `[gameplay] unlock_all_sparring_characters = true` makes every Sparring character available without campaign progression. Enabled by default; does not change saved Story unlocks.

- **Shadow and Titan in Sparring**. Enabled by default with `[gameplay] enable_shadow_in_sparring` and `enable_titan_in_sparring`. Shadow uses his equipped campaign gear; Titan uses his own sword, ranged arm and magic. Move down past the third roster row to reveal more fighters at the original portrait size. See [roster settings](docs/runtime-guide.md#sparring-roster).

<p>
  <a href="docs/screenshots/character-select.png"><img src="docs/screenshots/character-select.png" width="49%" alt="The unlocked Sparring character roster with Fury ready and the footer hidden"></a>
  <a href="docs/screenshots/sparring-titan.png"><img src="docs/screenshots/sparring-titan.png" width="49%" alt="Titan fighting Shogun in Sparring, with his sword and boss-power indicator visible"></a>
</p>

*Unlocked character selection · Titan versus Shogun in Sparring.*

### Boss refights and movies

- **Boss refights and completed save**. Defeated bosses and Gates of Shadows have a Refight button with an X shortcut, including the six boss encounters after the Gates. Refights grant no rewards and leave saves unchanged. APKs and native packages bundle a completed save, selected with `[storage] use_completed_save = true`. Disabled by default; uses a separate profile.

- **Unlocked Extras movies**. `[gameplay] unlock_all_videos = true` enables gallery movie playback without changing watched-video save flags. Enabled in the supplied INI.

- **Intro and gameplay toggles**. Skip the intro with mapped A/B, Space, or a mouse click, or disable it with `[gameplay] show_intro = false`. Startup data and GPU heap preparation run during playback to reduce the wait after skipping. `ai_enabled` and `show_time_results` expose the original AI and timing-display controls.

<p>
  <a href="docs/screenshots/boss-refight.png"><img src="docs/screenshots/boss-refight.png" width="49%" alt="Completed Lynx encounter with the Refight button visible on the campaign map"></a>
  <a href="docs/screenshots/extras-movies.png"><img src="docs/screenshots/extras-movies.png" width="49%" alt="The unlocked Extras video gallery showing The Beginning and Gates of Shadows"></a>
</p>

*Completed Lynx encounter with Refight · Unlocked Extras movies.*

### Developer tools

- **Desktop developer console**. Opt in with `[developer] console = true`; **F1** or **backtick** opens the original console with desktop text input, command history and scrolling.

- **Fight trainer and shop cheats**. Optional `[developer] trainer` and `shop_cheats` expose the original fight controls and local shop actions through the console. Disabled by default; currency, items, levels and fight results can be saved by the game.

- **Debug overlays**. Opt in with `[developer] overlays = true`, then use **F2** to toggle the original FPS, memory, build and model/animation displays in supported scenes.

<p>
  <a href="docs/screenshots/developer-console.png"><img src="docs/screenshots/developer-console.png" width="49%" alt="The desktop developer console displaying available trainer commands"></a>
  <a href="docs/screenshots/debug-overlays.png"><img src="docs/screenshots/debug-overlays.png" width="49%" alt="An active fight with a trainer command applied and FPS, memory and animation debug overlays visible"></a>
</p>

*Console and trainer commands · Trainer action and combat debug overlays.*

### LAN Sparring

- **LAN Sparring (experimental)**. Native Normal/LAN and Host/Join menus, local host discovery and persistent two-player sessions. Both devices must use the same SF2Recomp release and matching roster settings. No Python launcher required. See the [LAN guide](docs/lan-sparring.md).

<p>
  <a href="docs/screenshots/lan-mode.png"><img src="docs/screenshots/lan-mode.png" width="49%" alt="Sparring menu offering Normal and LAN Multiplayer modes"></a>
  <a href="docs/screenshots/lan-host-join.png"><img src="docs/screenshots/lan-host-join.png" width="49%" alt="LAN Multiplayer menu offering Host, Join and Back"></a>
</p>

*Normal and LAN choices · Native Host and Join menu.*

### Android touch controls

The Android build uses the retained mobile joystick and action buttons, with
simultaneous movement and attacks. Sparring characters with a special ability
also get a boss-power button. Keyboard and controller input remain available.
See [Android development](#android) for setup and device requirements.

[![Lynx versus Corsair on Android, with touch controls and the boss-power button ready](docs/screenshots/android-sparring.png)](docs/screenshots/android-sparring.png)

*Android Sparring with touch controls and Lynx's boss power ready.*

### Saves and offline settings

- **Game network blocking**. `[network] enabled = false` is the default and blocks game connections, DNS, uploads, online multiplayer, external web links and the remote developer listener. Offline Sparring remains available. Enabling networking does not implement missing online services.

- **Configurable save location and imports**. `[storage] save_directory` selects a portable or custom save folder. Existing `userdata/` backups can be imported under its `save_0/` directory.

See the [settings and controls guide](docs/runtime-guide.md) for setup, commands
and limitations, and the [supplied INI](sf2recomp.ini) for shipped defaults.
Full-game stability and timing at every supported frame-rate setting remain
unverified; see [Status](#status).


## Development

[Build from source](docs/source-guide.md#requirements) ·
[Windows](docs/windows.md) · [macOS](docs/macos.md) ·
[Android development](docs/source-guide.md#android-development) ·
[Release builds and versioning](docs/releases.md) ·
[Development and verification](docs/development.md)

Linux x86-64 is the tested desktop development target. Windows x86-64 and macOS
Apple Silicon support are experimental. Android arm64/x86-64 development
builds exist; see the source guide for validation details.

## License

Project additions use the [MIT license](LICENSE). Bundled dependencies retain
their own licenses; see [third-party notices](THIRD_PARTY_NOTICES).
This is an independent fan project with no affiliation to Nekki or Nintendo.
Game content belongs to its respective owners. Converted output contains your
game files and translated game code; keep it private.
