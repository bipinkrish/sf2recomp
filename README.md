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
Combat remains unverified. Original guest C++ catch/landing-pad support and video
playback are unfinished. Windows remains a porting target, not a working build.

The verified rendering configuration is X11/Xwayland with Mesa lavapipe software
Vulkan. This machine's AMD hardware path has encountered kernel-driver faults;
hardware acceleration is not validated. The CPU game code executes natively.

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
# Install the documented native development packages, then build SDL locally
# if your system does not provide SDL 3.4.0 or newer:
tools/setup_runtime.sh --build-sdl
tools/setup_runtime.sh --check

python3 tools/convert.py \
  --nsp "/path/to/Shadow Fight 2.nsp" \
  --keys "/path/to/your/prod.keys" \
  --output "$HOME/Games/Shadow Fight 2"
```

SDL headers/libraries and SMAA sources are local dependencies, excluded from Git.
`--build-sdl` prepares both. If SDL 3.4+ is already installed, run
`tools/setup_runtime.sh --fetch-smaa` followed by `--check` instead. `make` also
fetches missing SMAA files automatically; setup pins their revision and checksums.

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

[storage]
save_directory = save

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

`language = auto` follows the system locale. `SF2_CONFIG=/path/to/custom.ini`
selects another INI; relative overrides are resolved against the launch directory.
Precedence is built-in defaults, then INI, then existing `LARECOMP_*` session
overrides. Saves/caches/logs live beside the executable by default;
`LARECOMP_DATA_ROOT` or `LARECOMP_DATA_HOME` selects another writable state folder.
These overrides do not move the default executable-side INI.

The developer launcher `tools/run_linux.sh --software` uses the repository INI
and keeps run data under `build/run`.

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
