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

[Status](#status) · [Requirements](#requirements) ·
[Getting started](#getting-started) · [NSP extraction](#optional-extract-from-an-nsp) ·
[Settings and controls](#settings-and-controls) · [Development](#development)

## Status

This is a work in progress. Linux x86-64 builds run the animated menu, Story/Sensei
introduction and intro cinematic. Selected tutorial and Kenji sparring tests have
also passed; full combat stability and campaign completion remain unverified.

| Platform | Status |
| --- | --- |
| Linux x86-64 | Current development target; tested with AMD Radeon 680M, NVIDIA RTX 4070 Laptop GPU and Mesa lavapipe software Vulkan |
| Windows x86-64 | Experimental host/build/package support implemented; no verified native build. See the [Windows guide](docs/windows.md). |
| macOS | Requires porting work; no verified native build |

The CPU game code runs natively, with a Vulkan renderer and SDL for windowing,
input and audio. Keyboard/controller remapping, resolution scaling, FSR 1, SMAA
and persistent saves are available. Compatibility with other GPUs remains
unverified. See the [validation notes](docs/development.md#validation-notes) and
[working findings](docs/findings.md) for test results and historical measurements.

## Requirements

For the complete experimental Windows workflow, including local extraction and
symbol analysis, follow the [Windows guide](docs/windows.md).

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

| Input | Default keyboard binding |
| --- | --- |
| A / B / X / Y | K / J / L / I |
| Left stick | W / A / S / D |
| D-pad | Arrow keys |
| Plus / Minus | Return / Backspace |
| Skip intro | K or J |

Keyboard and gamepad bindings are editable in the INI. The main menu's **Exit**
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

## Licensing

Project additions use the [MIT license](LICENSE). Imported runtime and generator
code retains its original notices; see [third-party notices](THIRD_PARTY_NOTICES)
and [license texts](licenses/).

Game data and locally generated game-derived output are outside those software
licenses. The locally built executable contains translated original game code;
keep game files and generated output separate from public source distributions.
Use inputs you have the rights to use. This project is not affiliated with Nekki
or Nintendo.
