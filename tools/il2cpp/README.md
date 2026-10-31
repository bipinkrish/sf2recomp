# Local IL2CPP analysis tool

The repository tracks configuration and license notices, not compiled assemblies.
Build the official source with Python 3.12+ and the .NET 8 SDK:

```sh
python3 tools/build_il2cpp.py
python3 tools/analyze_il2cpp.py
```

The build downloads official Il2CppDumper v6.7.46 source, commit
`8a521b9c180cf13499253f0818cbc729dca767cb`, from
<https://github.com/Perfare/Il2CppDumper/tree/8a521b9c180cf13499253f0818cbc729dca767cb>.
The source archive SHA256 is
`a23ba541d2b085c5c1ca94c9c72cf217b88c8600589c4554285778ae4f4df2a8`.
It builds the original net6.0 target without a native apphost; the Python launcher
enables runtime roll-forward for .NET 8+. These entry points run on Windows and
Linux. The dumper is built automatically when analysis needs it, and its assembly
hashes are checked before reuse. Downloaded source, NuGet dependencies and
generated binaries remain local.

Upstream declares Mono.Cecil 0.11.4 as its NuGet dependency. Official notices are
retained centrally as `licenses/Il2CppDumper.txt` and `licenses/Mono.Cecil.txt`
relative to the repository root, retrieved from:

- <https://raw.githubusercontent.com/Perfare/Il2CppDumper/v6.7.46/LICENSE>
- <https://raw.githubusercontent.com/jbevain/cecil/0.11.4/LICENSE.txt>

Keep those notices with any redistributed tools. `config.json` disables the
interactive pause and unnecessary dummy assemblies. `GenerateStruct` stays enabled
because upstream writes the required `script.json` method map in that step. All dumped game metadata stays in ignored
`game/il2cpp`, never in the source distribution.
