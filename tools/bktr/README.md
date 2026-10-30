# tools/bktr — merged update-RomFS extraction (LibHac)

The update NCA's data section is a **BKTR delta**: a bucket-tree relocation of
patch data over the base RomFS. nstool cannot compose it (it takes one ticket at
a time, and its `--titlekey` path wants a key file it won't get right here), so
this vendored .NET tool uses LibHac to assemble the two NCAs' sections with both
tickets and clones the merged ivfc data layer.

The repository contains source only; generated .NET assemblies remain local.
Diagnostics print identifiers and section information, never decryption key values.

## Build

```sh
tools/bktr/build.sh        # = dotnet build -c Release (needs .NET 8 SDK)
```

`dotnet restore` pulls `LibHac 0.19.0` from NuGet (the upstream LibHac GitHub
repo was taken down in 2026; the NuGet package still exists).

## Usage (from `tools/extract.sh`)

```sh
SF2BKTR_DUMP_OUT=game/extracted/update/romfs.img \
    tools/bktr/bin/Release/net8.0/Sf2Bktr \
    <patch.nca> <base.nca> <prod.keys> game/nsp/title.keys
nstool -t romfs -x game/extracted/update/romfs game/extracted/update/romfs.img
```

Env toggles: `SF2BKTR_PROBE` (diagnostics), `SF2BKTR_DUMP_PATCH_EX` (also clone
the raw CTR-EX section: ivfc hash layers + patch data slots only), 
`SF2BKTR_FORCE_TK` (bypass keyfile parsing).

## Gotchas burned into this tool

* `title.keys` must contain the **ENCRYPTED** titlekeys — the raw 0x10 bytes at
  ticket offset 0x180. hactool / nstool / LibHac all decrypt them internally
  with the right `titlekek`. Plaintext keys double-decrypt
  into garbage sections.
* The ivfc data level of the patch section is the *virtual* rewrite: size
  `0x56D85738` at logical offset `0x2C8000`; five hash levels live in front of
  it (`0x4000` x4 + `0x2B8000`). BKTR tables: relocation block 0x8000,
  102 entries @ section-relative `0x5AB4600`; subsection block 0x8000,
  1 entry @ `0x5ABC600` (ends exactly at the section end).
* An update NCA keeps the **base** title id at header 0x210; only its rights id
  (0x230) is update-specific — pair tickets by rights id, never by title id.
