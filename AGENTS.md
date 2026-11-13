# SF2Recomp agent guide

SF2Recomp translates the Nintendo Switch release of Shadow Fight 2 into C and
links it with a native SDL3 and Vulkan runtime. The supported game input is
v1.1.0, Switch update `v131072`.

## Find the code

- `tools/convert.py` drives source conversion. `tools/extract.py` prepares NSP
  inputs, and `tools/recompr/` translates the main and SDK NSOs.
- `runtime/` contains guest loading, dispatch, and services. Look in
  `runtime/hle/` for game hooks, `runtime/ui/` for host menu input, and
  `runtime/nvn/` for GPU emulation and Vulkan rendering.
- `runtime/platform/` contains host-specific code. `android/` builds the APK
  with Gradle and CMake. `tools/release.py` and `tools/ci_release.py` handle
  portable desktop conversion and release builds, respectively.
- `sf2recomp.ini` contains the shipped runtime defaults. Check it alongside
  `runtime/settings.c` and the [runtime guide](docs/runtime-guide.md) when
  changing a setting.

## Build and verify

Run commands from the repository root. Use Python 3.12 or later for the Python
commands. See the [source setup guide](docs/source-guide.md) for dependencies
and conversion.

| Task | Command and prerequisites |
| --- | --- |
| Run one test module | `python3 -m unittest discover -s tests -p test_settings.py -v`. Replace `test_settings.py` with the module's filename. |
| Run the full suite | `python3 -m unittest discover -s tests -v`. Some tests compile native code. Optional integration cases need additional tools or a local game installation. |
| Audit fixed values | `python3 tools/centralize_literals.py --check`, also run by desktop release CI. |
| Compile desktop runtime objects | `make -j8 runtime-objs`. This works without game files. The build can fetch missing native dependencies. |
| Regenerate translated C | `make gen` with the updated ExeFS and IL2CPP symbol map. Default output is `game/gen/`. |
| Build the desktop game | `make -j8` after generation. On Linux, `tools/run_linux.sh` launches the local game and keeps run data under `build/run/`. |

Use [development and verification](docs/development.md) for GPU diagnostics and
local play checks, and the [Windows](docs/windows.md), [macOS](docs/macos.md),
and [Android](docs/source-guide.md#android-development) guides for host-specific
commands. Report which platform and scenario you exercised when validating
runtime behavior.

## Preserve project contracts

- Edit the generator or hook definitions when translated C needs to change.
  Regenerate after changing `tools/recompr/patches.py` or
  `runtime/network_hooks.inc`. Preserve the main and SDK NSO build-ID checks
  that run before boot. See the [generator guide](tools/recompr/README.md).
- Keep user game dumps, extracted assets, keys, generated C, converted desktop
  builds, and game APKs private. Keep them out of commits and release archives,
  and keep key values out of logs.
- Keep game networking disabled by default and preserve existing settings
  and saves on reconversion. For LAN changes, use `runtime/lan_protocol.h` and
  follow the compatibility rules in the [release guide](docs/releases.md).
- Update fixed values in the catalog that owns them. Use the
  [constants guide](docs/constants.md) to find the definition and audit the change.
