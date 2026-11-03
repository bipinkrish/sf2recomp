# Find and edit fixed values

Fixed values are grouped by the subsystem that owns them:

| Definitions | Owns |
| --- | --- |
| `runtime/version.h` | SF2Recomp's semantic version |
| `runtime/lan_protocol.h` | LAN magic, packet types, protocol, simulation revision, roster flags and selection salt |
| `runtime/constants/*.h` | Native core, services, platform, HLE, renderer, Maxwell decoder and Vulkan values and strings |
| `tools/_sf2_tool_constants.py` | Conversion, extraction, packaging and developer-tool values and strings, grouped by module |
| `tools/recompr/_sf2_recompiler_constants.py` | ARM64 decoder masks, register layouts, offsets, code templates and messages |
| `android/app/src/main/java/org/sf2recomp/app/GameConstants.java` | Android storage/import values and messages |
| `tools/bktr/Sf2Constants.cs` | BKTR helper values and messages |
| `android/toolchain.properties` | Android versions shared by the downloader and Gradle |

Edit a definition in its catalog to change a value. Existing named declarations
retain their semantic names, such as `OFF_SP` or `UPDATE_BUILD_ID`. Other names
identify the original module and value; a short suffix distinguishes different
literal spellings. Source-path comments identify their owner. Keep semantically
different settings separate even when they currently have equal values.

`SF2RECOMP_LAN_SELECTION_SALT` separates the menu-selection random stream. It is
mixed with the host's match seed. It does not force a fixed match seed. The host
chooses the seed, sends it to its peer and advances it for the next match.

## Check a change

```sh
python3 tools/centralize_literals.py --check --report build/constants-audit.json
```

To move new fixed values into their subsystem catalogs:

```sh
python3 tools/centralize_literals.py --write
python3 tools/centralize_literals.py --name-declarations --prune --check
```

Review the names and assign semantic names where a value expresses a domain
rule. The migration verifies Python syntax-tree equivalence after re-expanding
references and exact C/Java/C# token text after re-expansion. Compilation and
runtime tests still check language and integration behavior.

The audit covers project runtime C/C++/include fragments, Python tools, Android
Java and the C# extraction helper. Third-party code, generated game code and
independent test fixtures keep their values. Comments, documentation, import and
include syntax, preprocessor diagnostics and interpolated string fragments stay
in their language's normal form. Literal spellings used for macro token pasting
also stay in place. Ordinary zero/one arithmetic and identity values remain inline.
The version/protocol files and shared network-hook table already define named
constants and are not rewritten. Build-system manifests retain their declarative
syntax; shared Android toolchain values come from the properties file above.
The migration engine and isolated interpreter bootstrap keep the syntax needed
to load the catalogs before catalog imports are available.

Source-slice tests use `tests/source_values.py` to expand catalog references
before compiling an extracted function. Tests that compile production headers
or whole translation units consume the catalogs directly.
