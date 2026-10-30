# Local input preparation and compilation tools

This directory contains source tools for preparing the supported SF2 inputs and
translating them locally. It does not contain game files or user decryption keys.
Downloaded and built tools stay in ignored directories.

## Requirements

The currently supported host is Linux x86_64 with Python 3.12+, a .NET 8 SDK,
C/C++ build tools, CMake and make. Native game compilation additionally needs the
Clang/LLD, SDL3, Vulkan and shaderc dependencies described in the root README.

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/requirements.txt
tools/build_extraction_tools.sh
python3 tools/build_il2cpp.py
```

Install the .NET **SDK** from
<https://dotnet.microsoft.com/en-us/download/dotnet/8.0>; a runtime alone cannot
build the tools. Add its directory to `PATH`, or set `DOTNET=/absolute/path/to/dotnet`.
No system installation is required when using Microsoft's local SDK archive.
The Il2CppDumper build verifies its pinned official source archive before building;
see [its provenance and notices](il2cpp/README.md). The BKTR merger restores pinned
`LibHac 0.19.0` from NuGet. First builds need network access for source/NuGet
packages; source caches and package caches can be reused offline afterward.

`build_extraction_tools.sh` automatically fetches hactool, NSTool and NSTool's
five shared dependencies from official source archives before building them.
Their immutable revisions and SHA256 hashes are recorded in
`extraction-dependencies.json`. The pinned archives reproduce all 1,862 formerly
vendored source/license files byte for byte; no local source patches are required.
Sources under `tools/third_party/` and executables under `tools/bin/` are ignored.
Git submodule initialization is not needed. Original upstream notices remain in
the downloaded source trees; tracked license copies live centrally in `licenses/`.

Verified archive caches default to `build/dependencies/archives`.
`SF2_SOURCE_ARCHIVES=/path/to/cache` selects another cache; copy the seven pinned
archives there for offline setup. `python3 tools/setup_extraction.py --offline`
prepares sources without downloads; `--check` verifies existing source files.
Subsequent builds reuse prepared sources and cached archives.

`requirements.txt` pins the Python versions used during validation. Capstone and
Unicorn analyze instructions/jump tables **during generation**; the native runtime
executes translated C. LZ4 reads NSOs; cryptography validates user-supplied keys.

## Prepare a supported package

```sh
tools/extract.sh '/path/to/your/base-and-update.nsp' '/path/to/your/prod.keys'
tools/il2cpp_run.sh
```

Supported input currently means the **combined SF2 base + v131072 update** NSP
containing its two tickets and `.cnmt.xml` sidecars. The title IDs, update version,
entry names/ranges, and extracted update main build ID are checked. Separate
base/update packages, compressed NSZ, and bundles without XML metadata are not yet
supported and fail with an explanation. Supplying a licensed copy alone does not
make every package/version compatible with this implementation.

`--validate-only` checks packaged layout without decryption. `--game-dir DIR`
selects a local output root (default: ignored `game/`). New extraction is staged in
that root; failures preserve the previous `nsp/` and `extracted/` directories.
Successful output includes `extracted/update/{exefs,romfs}` for compilation/runtime
and the original base extraction for development. Tickets/titlekeys remain local
with private directory/file permissions, and the normal tools do not log key values.

For an alternate extracted directory and symbol output:

```sh
tools/il2cpp_run.sh /path/to/extracted/update /path/to/local/symbols
```

Generate/build from those local inputs using `tools/recompr/README.md` or the root
conversion workflow. Generated C, symbols, metadata, native game binaries and game
assets must stay out of a source-only repository and release.

## Tests and licenses

```sh
python3 -m unittest discover -s tests -v
```

Extraction tests use synthetic containers/keys, including invalid inputs,
publication rollback, and key-log redaction. Source/metadata provenance is in
`extraction-dependencies.json`, `../THIRD_PARTY_NOTICES` and `il2cpp/README.md`; retain dependency licenses with any
distribution. Third-party cryptography test keys are fixtures, not user keys.

## Official game metadata and desktop launcher

Extraction selects the update's control NCA, falling back to base control only
when the update declares no control content. It keeps the raw `control.nacp`,
converts the matching official JPEG icon to `icon.png` without resizing, and
writes `game-info.json` in the updated game directory. These are local game-derived
outputs, never tracked source assets. The display version comes from the NACP
field at `0x3060`; the package ordinal `v131072` is a separate value. In the validated
bundle the base display version is **1.0.0** and the update display version is **1.1.0**.

Conversion places the raw metadata, icon and a desktop launcher beside the native
executable. Its stable application ID/desktop filename is
`org.sf2recomp.ShadowFight2`; the menu name is `Shadow Fight 2 (sf2recomp)`.
The desktop entry's `Version=1.0` describes the desktop file format, while
`X-AppVersion` records the official game display version.

No application-menu entry is installed by default. To register a converted output
in your own application menu, explicitly request it during conversion or packaging:

```sh
python3 tools/convert.py --game /path/to/extracted/update --package-only \
  --output /path/to/native-game --install-desktop
```

This copies only the launcher into `$XDG_DATA_HOME/applications` (default
`~/.local/share/applications`); it references the executable and icon in your local
output folder. Regenerate/reinstall the launcher if that folder moves. Removing
that single desktop file removes the application-menu registration.
