# Shadow Fight 2 Recomp

A static recompilation of **Shadow Fight 2 for Nintendo Switch** into a native application. You supply your own extracted game files, then build the game
locally from this compiler and runtime source.

The Switch version was chosen because it includes **Sparring mode**, which offers
**offline player-versus-player (PvP)** fights.

> [!IMPORTANT]
> **No game files, assets, keys or generated game code are included.**
> The supported game is **v1.1.0 (Switch update v131072)**.
> **Already extracted files do not require `prod.keys`.** Keys are only needed
> if you use the optional NSP extraction workflow below. Recompilation and running
> the converted game do not require keys.

[Status](#status) · [Features](#features) · [Requirements](#requirements) ·
[Getting started](#getting-started) · [NSP extraction](#optional-extract-from-an-nsp) ·
[Settings and controls](#settings-and-controls) · [Development](#development) ·
[Legal](#legal)

## Status

This is a work in progress. Linux x86-64 builds run the animated menu, Story/Sensei
introduction and intro cinematic. Selected tutorial and Kenji sparring tests have
also passed; full combat stability and campaign completion remain unverified.

| Platform | Status |
| --- | --- |
| Linux x86-64 | Current development target; tested with AMD Radeon 680M, NVIDIA RTX 4070 Laptop GPU and Mesa lavapipe software Vulkan |
| Windows x86-64 | Experimental host/build/package support implemented; no verified native build. See the [Windows guide](docs/windows.md). |
| macOS arm64 / x86-64 | Experimental host/build/conversion support implemented; native build and GPU validation pending. See the [macOS guide](docs/macos.md). |

The CPU game code runs natively, with a Vulkan renderer and SDL for windowing,
input and audio. Keyboard/controller remapping, resolution scaling, FSR 1, SMAA
and persistent saves are available. Compatibility with other GPUs remains
unverified. See the [validation notes](docs/development.md#validation-notes) and
[working findings](docs/findings.md) for test results and historical measurements.

## Features

The recompilation adds desktop features and graphics options beyond the Switch
release, and exposes some of the game's original developer tools. Settings below
live in **`sf2recomp.ini` beside the converted executable** and take effect after
restarting; no rebuild is needed.

| Feature | What it provides |
| --- | --- |
| **Aspect ratios (experimental)** | `[video] aspect_ratio` accepts `16:10`, `21:9` or `32:9`, with native screen sizes of 1728×1080, 2520×1080 and 3840×1080 before uniform scaling. Restart after changing; `original` / `16:9` is the default. In 32:9, scenery is enlarged uniformly to cover the sides, cropping some background detail vertically. Fighter and HUD proportions are preserved. Movies keep their original proportions. |
| **Native display resolution** | `[video] internal_resolution = auto` or `match` renders at the window's fitted drawable pixel size, following window resizing and fullscreen. Higher resolutions improve geometry edges; bitmap artwork and font atlases retain their original detail. |
| **Resolution scaling** | Fixed internal-resolution presets of `0.25x`, `0.50x`, `0.75x`, `1x`, `2x`, `3x` and `4x` let you trade rendering detail for performance, within the GPU's texture-size limit. |
| **Dynamic resolution** | `[video] internal_resolution = dynamic` adjusts render scale in response to GPU load to work toward the configured frame-rate target. |
| **AMD FSR 1 upscaling** | `[video] upscaler = fsr1` enables spatial upscaling and sharpening when the rendered image is smaller than its window area. Use a lower internal resolution to activate it; `auto` selects bilinear on integrated GPUs and FSR 1 on discrete GPUs. |
| **SMAA anti-aliasing** | `[video] antialiasing = smaa` enables optional post-process edge smoothing. Disabled by default. |
| **Configurable frame rate** | `[video] target_fps` accepts **30–360**, including targets above 60 FPS, and sets the game's target frame rate and presentation pacing. Achievable performance depends on hardware; set the host display refresh rate separately. |
| **Desktop window modes** | A resizable window or fullscreen, selected with `[video] fullscreen`. |
| **Remappable keyboard and controllers** | `[controls.keyboard]` and `[controls.gamepad]` map desktop inputs to Switch actions, including unbinding actions and sharing inputs. |
| **Keyboard prompts (experimental)** | `[experimental] keyboard_prompts = true` replaces supported in-game button hints with your mapped keys. Automatic selection follows controller presence. Disabled by default; some screens may need layout adjustments. |
| **Desktop Exit button** | The main menu's localized **Exit** option closes the game and returns to the desktop, including in fullscreen. |
| **Game network blocking** | `[network] enabled = false` is the default and blocks game connections, DNS, uploads, online multiplayer, external web links and the remote developer listener. Offline Sparring remains available. Enabling networking does not implement missing online services. |
| **Unlocked Sparring roster** | `[gameplay] unlock_all_sparring_characters = true` makes every Sparring character available without campaign progression. Enabled by default; does not change saved Story unlocks. |
| **Unlocked Extras movies** | `[gameplay] unlock_all_videos = true` enables gallery movie playback without changing watched-video save flags. Enabled in the supplied INI. |
| **Intro and gameplay toggles** | Skip the intro with either mapped A or B, or disable it with `[gameplay] show_intro = false`. `ai_enabled` and `show_time_results` expose the original AI and timing-display controls. |
| **Desktop developer console** | Opt in with `[developer] console = true`; **F1** or **backtick** opens the original console with desktop text input, command history and scrolling. |
| **Fight trainer and shop cheats** | Optional `[developer] trainer` and `shop_cheats` expose the original fight controls and local shop actions through the console. Disabled by default; currency, items, levels and fight results can be saved by the game. |
| **Debug overlays** | Opt in with `[developer] overlays = true`, then use **F2** to toggle the original FPS, memory, build and model/animation displays in supported scenes. |
| **Configurable save location and imports** | `[storage] save_directory` selects a portable or custom save folder. Existing `userdata/` backups can be imported under its `save_0/` directory. |
| **Desktop configuration** | The INI also provides language selection (including system-locale detection), master volume and reverb controls. Alternate configuration and writable data locations can be selected at launch. |

See the [settings and controls guide](docs/runtime-guide.md) for setup, commands
and limitations, and the [supplied INI](sf2recomp.ini) for shipped defaults.
Full-game stability and timing at every supported frame-rate setting remain
unverified; see [Status](#status).

## Requirements

For the complete experimental Windows workflow, including local extraction and
symbol analysis, follow the [Windows guide](docs/windows.md).
For Apple Silicon and Intel Macs, follow the [macOS guide](docs/macos.md).

To build and convert on **Linux x86-64**, install:

- Python 3.12+ and venv/pip.
- .NET 8 **SDK** for the IL2CPP analysis tools.
- Clang/Clang++, LLD, GNU Make, CMake, Ninja, pkg-config and source download utilities.
- Vulkan 1.3+ headers/loader, shaderc, Vulkan Memory Allocator headers and the
  display/audio development libraries listed in the
  [native dependency guide](tools/runtime-dependencies.md).

Follow that guide for distribution-specific package commands. Conversion prepares
missing SDL 3.4+, SMAA sources, fallback fonts and the bundled FFmpeg decoder
automatically. Initial setup needs network access for pinned sources and packages.
A local .NET SDK can be selected with `DOTNET=/absolute/path/to/dotnet`.

## Getting started

Run the following commands from the repository root.

### 1. Prepare your extracted game files

Use your own legally obtained files from **v1.1.0 / update v131072**, with the
update already applied. The converter expects this layout:

```text
/path/to/extracted/update/
  exefs/
    main
    sdk
    main.npdm
  romfs/
    Data/
      Managed/Metadata/global-metadata.dat
      ...                               remaining updated game assets
  control.nacp                          official game metadata
  icon.png                              matching official icon
```

Supply the complete updated RomFS. The metadata and icon are required for
packaging; `exefs` and `romfs` alone are insufficient. The converter checks the
supported main build ID, and the executable checks matching main/SDK identities
before boot.

**No `prod.keys` or tickets are needed for this workflow.** If your input is an
encrypted NSP instead, use [NSP extraction](#optional-extract-from-an-nsp).

### 2. Set up Python

After installing the native requirements above:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/requirements.txt
```

### 3. Build and convert

```sh
python3 tools/convert.py \
  --game "/path/to/extracted/update" \
  --output "$HOME/Games/Shadow Fight 2"
```

This generates C from your game files, compiles the native executable and creates
the output folder. Conversion builds missing analysis tools as needed and uses up
to eight compiler jobs by default; use `--jobs N` to change that.

### 4. Run

```sh
cd "$HOME/Games/Shadow Fight 2"
./sf2
```

Keep the output folder together: it contains the executable, extracted assets,
`sf2recomp.ini`, metadata, runtime libraries, fonts and license notices. System
Vulkan/shaderc and C/C++ runtime libraries remain required.

Add `--install-desktop` to the conversion command to register the game in your
user application menu. If you move the output folder, rerun conversion with that
option to update the launcher's absolute paths. Re-running conversion preserves
existing output settings and saves.

## Optional: extract from an NSP

**`prod.keys` is required only for decrypting/extracting the NSP.** It is not
required for recompilation from extracted files or for running the converted game.

After completing the requirements and Python setup above, use:

```sh
python3 tools/convert.py \
  --nsp "/path/to/Shadow Fight 2.nsp" \
  --keys "/path/to/your/prod.keys" \
  --output "$HOME/Games/Shadow Fight 2"
```

This performs extraction and then the same build and packaging steps. It takes
the update ExeFS, merges the updated RomFS over the base and extracts the official
metadata/icon. Keys remain local, are not logged and are excluded from the
converted output.

The supported input is a **combined base + update v131072 NSP containing its
CNMT XML sidecars and tickets**. Separate base/update packages, NSZ files and
other layouts are not currently supported. Extraction stages privately and
publishes only after validation succeeds.

For extraction tools, supported layouts and offline setup, see the
[tooling guide](tools/README.md).

## Settings and controls

Edit **`sf2recomp.ini` beside the converted executable**, then restart the game.
Settings are read at launch; changing them does not require rebuilding.

Game networking is disabled by default with `[network] enabled = false`, including
DNS, uploads, multiplayer and external web links. Existing INIs without this
section also default to offline. See the [offline networking settings](docs/runtime-guide.md#offline-networking).

| Input | Default keyboard binding |
| --- | --- |
| A / B / X / Y | K / J / L / I |
| Left stick | W / A / S / D |
| D-pad | Arrow keys |
| Plus / Minus | Return / Backspace |
| Skip intro | K or J |

Keyboard and gamepad bindings are editable in the INI. For keyboard P1 versus
controller P2 in Sparring, set `[input] player_setup = keyboard_vs_controller`
and restart. `solo` preserves shared keyboard/first-controller P1 input;
`two_controllers` assigns one controller per player and disables keyboard gameplay
input. See [player assignment](docs/runtime-guide.md#player-assignment).
The main menu's **Exit**
option returns to the desktop, including in fullscreen.

Saves default to `save/` inside the converted folder. Change
`[storage] save_directory` to choose another location. Sparring characters are
unlocked by default; set `[gameplay] unlock_all_sparring_characters = false`
to restore save-based unlocks.

See the [settings and controls guide](docs/runtime-guide.md) for graphics,
language, save imports, controller prompts and optional developer tools.
The [supplied INI](sf2recomp.ini) lists the shipped settings and bindings.

## Development

After an initial conversion has generated the game code:

```sh
make -j8
```

Run the unit tests without game files or a GPU:

```sh
python3 -m unittest discover -s tests -v
```

- [Development and verification](docs/development.md): regeneration, local
  launcher, GPU diagnostics and validation notes.
- [Generator guide](tools/recompr/README.md): translating the main and SDK modules.
- [Working findings](docs/findings.md): historical investigation notes.

## Legal

Shadow Fight 2 Recomp is an independent fan project, with no affiliation,
endorsement or sponsorship from Nekki or Nintendo. Shadow Fight 2, Nintendo
Switch and related names, trademarks and game content belong to their respective
rights holders.

This repository contains recompilation tools and runtime source, with no original
game code, game assets, game fonts, firmware, tickets or decryption keys. You must
supply your own legally obtained game files to build and play.

Conversion happens locally. The resulting executable contains translated original
game code, and the converted folder includes your supplied game assets. Keep game
files, generated code and converted output separate from public source
distributions. The project's software licenses grant no rights to that game
material.

## Licensing

Project additions use the [MIT license](LICENSE). Imported runtime and generator
code retains its original notices; see [third-party notices](THIRD_PARTY_NOTICES)
and [license texts](licenses/).
