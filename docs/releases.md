# Build and publish SF2Recomp

## Version the tool

The tool version is independent of the original game's v1.1.0 display version.
Update `SF2RECOMP_VERSION` in `runtime/version.h`, then tag the commit with `v`
followed by that version. The workflow rejects a tag that disagrees with the source.
`sf2recomp --version`, the converted game's `--version`, archive filenames and
the native runtime use this version.

LAN constants live in `runtime/lan_protocol.h`. The packet magic identifies SF2
traffic. The protocol number describes the packet format. The simulation ID
and compatibility revision describe lockstep behavior. The greeting also
requires the same tool version. Roster settings still participate in the
existing simulation check. Bump the protocol when packet layouts change, and
the compatibility revision when input, RNG, initialization or hashing changes.

## Build desktop packages

`.github/workflows/release.yml` builds Linux x86-64, Windows x86-64 and macOS
Apple Silicon on native runners. Pull requests affecting the build and manual
runs produce downloadable workflow artifacts. Pushing a matching `v*` tag
publishes all three checked archives and SHA-256 files to GitHub Releases after
every platform succeeds. No game files or keys are supplied to CI.

Local release builds require the developer dependencies for the build host,
the .NET 8 SDK, and a relocatable Python 3.12 prefix from
python-build-standalone. Install both requirements files into that prefix:

```sh
uv python install 3.12.12
uv pip install --python /path/to/portable/python --system --break-system-packages \
  -r tools/requirements.txt -r tools/release-requirements.txt
/path/to/portable/python tools/ci_release.py build
```

`--break-system-packages` applies only to this disposable managed Python prefix.
Do not point it at your operating system's Python.

The release builder compiles project-owned runtime objects and extraction tools,
publishes self-contained .NET helpers, and copies an explicit source allowlist.
It never copies local `game/` or generated game objects. Linux/macOS use a bundled
Zig C compiler; Windows uses a packaged native UCRT64 Clang/LLD toolchain. Linux
packages target Ubuntu 24.04's glibc baseline. macOS packages have the minimum OS
required by their bundled Homebrew libraries and remain unsigned development
distributions.

`tools/ci_release.py` moves the package into a path containing spaces, hides
developer tools from `PATH`, compiles a synthetic guest module, links the full
prebuilt runtime and checks matching synthetic NSO identities. It exercises the
Python native modules and starts the bundled extraction/analysis helpers. The
archive contains no synthetic fixtures or verification caches. Native CI checks
do not prove GPU compatibility or full-game playability.

## Player conversion

The installed release owns its interpreter, input helpers, compiler, native
libraries and writable work directories. `tools/release.py` calls the existing
extractor and generator, then compiles only the user's translated C and links it
with `native/runtime.a`. Settings and saves retain the converter's existing
preservation behavior. One file lock prevents simultaneous conversions from
writing the same generated directory.

The alternative was shipping a full developer checkout with shell tools and
development packages. Prebuilding the runtime keeps platform SDKs and build
systems on CI and makes desktop conversion independent of the player's setup.
Private generated game code remains on the player's machine.

The [constants guide](constants.md) maps fixed values and strings to their
subsystem catalogs and documents the audit command.

## Optional Android extension

`--android` downloads command-line SDK tools, Java and the pinned SDK/NDK/CMake
packages into `extensions/android` only when requested. Google's published
SHA-256 checks the bootstrap archive. A pinned Temurin release and its published
checksum select Java. `sdkmanager` verifies and installs the Google packages;
the Gradle wrapper verifies its pinned distribution checksum. A readiness stamp
and component checks let later runs reuse an installation or resume an incomplete
one. The extension keeps SDK state, Gradle caches and user preferences local.

The Android build uses portable C generation and the existing Gradle/CMake
project on any supported desktop host. It produces a private APK containing the
user's translated code and game files. It does not publish APKs through CI.
Windows/macOS Android builds still require native validation. The desktop ZIP
does not include the SDK/NDK, Java, an emulator or Android Studio.

The desktop workflow runs offline Android setup tests, which check paths and
cached toolchain state. It does not download Android tools or compile an APK on
any runner, including macOS.
